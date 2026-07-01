#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*

Copyright (c) 2026 Misael Díaz-Maldonado
This source file is released under the MIT License.
See LICENSE file in the project root for the full license information.

*/

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include "engine.hpp"

#define internal static
#define persistent static
#define _NET_WM_STATE_TOGGLE 2
#define KBD_ESC XKeysymToKeycode(display, XK_Escape)
#define KBD_F11 XKeysymToKeycode(display, XK_F11)
#define BLUE_MASK_SONIC (1L << 0)
#define Blue(r, g, b) ((((r) >= 0x30) && ((r) < 0x60)) && (((g) >= 0x30) && ((g) < 0x60)) && (((b) >= 0x90) && ((b) <= 0xff)))

// defines the handmade-hero Assert() macro function for those that know Casey Muratori's legendary game engine development series
#if DEVBUILD
#define Assert(x)\
	if (!(x)) {\
		fprintf(stderr, "assertion failed %s:%d\n", __FILE__, __LINE__);\
		*((volatile int*) 0) = 0;\
	}
#else
#define Assert(x)
#endif

#define ENGINE_FPS_TARGET 30.0f

// clusters (or groups) nodes that belong to Sonic
internal int Clustering(
		int32_t * const part,
		int32_t const * const frame,
		int64_t const red_mask,
		int64_t const green_mask,
		int64_t const blue_mask,
		int64_t const red_shift,
		int64_t const green_shift,
		int64_t const blue_shift,
		int64_t const width,
		int64_t const x,
		int64_t const y
) {
	int32_t const rgb = frame[x];
	int64_t const r = ((red_mask & rgb) >> red_shift);
	int64_t const g = ((green_mask & rgb) >> green_shift);
	int64_t const b = ((blue_mask & rgb) >> blue_shift);
	if (Blue(r, g, b)) {
		if (x > 0) {
			int32_t const rgb = frame[x - 1];
			int64_t const r = ((red_mask & rgb) >> red_shift);
			int64_t const g = ((green_mask & rgb) >> green_shift);
			int64_t const b = ((blue_mask & rgb) >> blue_shift);
			if (Blue(r, g, b)) {
				int32_t const id = (y * width + (x - 1));
				if (*(part + id) < 0) {
					*(part + id) -= 1;
					*(part + y * width + x) = id;
				}
				else {
					int32_t const root = *(part + id);
					Assert(*(part + root) < 0);
					*(part + y * width + x) = root;
					*(part + root) -= 1;
				}
			}
		}
	}

	return 0;

//err_cluster:
//	{
		// NOTE:
		// If the partition value for a root node is positive that means that
		// there is a logic error because root-nodes only store counts and
		// these are negative values to differentiate them easily from node ids.
//		fprintf(stderr, "%s\n", "error: clustering logic");
//		return -1;
//	}
}

internal void MergeClusters(
		struct cluster * const curr,
		struct cluster * const next,
		struct cluster * const clusters,
		int64_t const super
) {
	struct cluster *iter = &clusters[curr->next];
	while (iter->next != iter->id) {
		Assert(BLUE_MASK_SONIC == iter->mask);
		iter = &clusters[iter->next];
	}
	iter->next = next->id;
	next->prev = iter->id;
	next->super = super;
}

internal void CheckBoundsAndMerge(
	struct cluster * const curr,
	struct cluster * const next,
	struct cluster * const clusters,
	int64_t const super,
	int64_t const x_l,
	int64_t const x_u
) {
	if ((next->x >= x_l) && (next->x <= x_u)) {
		if (curr->next != curr->id) {
			MergeClusters(curr, next, clusters, super);
			struct cluster *iter = &clusters[next->next];
			do {
				iter->super = super;
				iter = &clusters[iter->next];
			} while (iter->next != iter->id);
			return;
		}
		else {
			curr->next = next->id;
			next->prev = curr->id;
			next->super = super;
			struct cluster *iter = &clusters[next->next];
			do {
				iter->super = super;
				iter = &clusters[iter->next];
			} while (iter->next != iter->id);
			return;
		}
	}
	else if (next->size > 1) {
		struct cluster const * const node = &clusters[next->node];
		if ((node->x >= x_l) && (node->x <= x_u)) {
			if (curr->next != curr->id) {
				MergeClusters(curr, next, clusters, super);
				struct cluster *iter = &clusters[next->next];
				do {
					iter->super = super;
					iter = &clusters[iter->next];
				} while (iter->next != iter->id);
				return;
			}
			else {
				curr->next = next->id;
				next->prev = curr->id;
				next->super = super;
				struct cluster *iter = &clusters[next->next];
				do {
					iter->super = super;
					iter = &clusters[iter->next];
				} while (iter->next != iter->id);
				return;
			}
		}
		else if ((next->x < x_l) && (node->x > x_u)) {
			// by continuity one of the nodes satisfies [x_l, x_u]
			if (curr->next != curr->id) {
				MergeClusters(curr, next, clusters, super);
				struct cluster *iter = &clusters[next->next];
				do {
					iter->super = super;
					iter = &clusters[iter->next];
				} while (iter->next != iter->id);
				return;
			}
			else {
				curr->next = next->id;
				next->prev = curr->id;
				next->super = super;
				struct cluster *iter = &clusters[next->next];
				do {
					iter->super = super;
					iter = &clusters[iter->next];
				} while (iter->next != iter->id);
				return;
			}
		}
	}
	else if (next->next == next->id) {
		return;
	}

	int merged = 0;
	struct cluster *iter = &clusters[next->next];
	do {
		if (iter->y != next->y) {
			merged = 0;
			break;
		}

		if ((iter->x >= x_l) && (iter->x <= x_u)) {
			if (curr->next != curr->id) {
				MergeClusters(curr, next, clusters, super);
				merged = 1;
				break;
			}
			else {
				curr->next = next->id;
				next->prev = curr->id;
				next->super = super;
				merged = 1;
				break;
			}
		} else if (iter->size > 1) {
			struct cluster const * const node = &clusters[next->node];
			if ((node->x >= x_l) && (node->x <= x_u)) {
				if (curr->next != curr->id) {
					MergeClusters(curr, next, clusters, super);
					merged = 1;
					break;
				}
				else {
					curr->next = next->id;
					next->prev = curr->id;
					next->super = super;
					merged = 1;
					break;
				}
			}
			else if ((iter->x < x_l) && (node->x > x_u)) {
				if (curr->next != curr->id) {
					MergeClusters(curr, next, clusters, super);
					merged = 1;
					break;
				}
				else {
					curr->next = next->id;
					next->prev = curr->id;
					next->super = super;
					merged = 1;
					break;
				}
			}
		}
		iter = &clusters[iter->next];
	} while (iter->next != iter->id);

	if (!merged) {
		return;
	}

	iter = &clusters[next->next];
	do {
		iter->super = super;
		iter = &clusters[iter->next];
	} while (iter->next != iter->id);

	return;
}

internal void MergeSuperClusters(
		struct cluster * const curr,
		struct cluster * const next,
		struct cluster * const clusters,
		int64_t const superid,
		int64_t const x_l,
		int64_t const x_u
) {
	Assert(-1 != superid);
	Assert(-1 != next->super);
	Assert(superid != next->super);
	Assert(curr->super != next->super);
	Assert(BLUE_MASK_SONIC == curr->mask);
	Assert(BLUE_MASK_SONIC == next->mask);
	if (next->x > x_u) {
		return;
	}
	else if (next->x < x_l) {
		int mergeable = 0;
		struct cluster const *iter = &clusters[next->next];
		do {
			if (iter->y != next->y) {
				break;
			}

			if (iter->x >= x_l) {
				mergeable = 1;
				break;
			}
			iter = &clusters[iter->next];
		} while (iter->next != iter->id);

		if (iter->next == iter->id) {
			if ((iter->size > 1) && (iter->y == next->y)) {
				struct cluster const *node = &clusters[iter->node];
				if ((node->x >= x_l) && (node->x <= x_u)) {
					mergeable = 1;
				}
				else if (node->x > x_u) {
					mergeable = 1;
				}
			}
		}

		if (!mergeable) {
			return;
		}
	}

	struct cluster *super = &clusters[superid];
	Assert(super->prev == super->id);

	struct cluster *merge = &clusters[next->super];
	Assert(merge->prev == merge->id);

	int64_t id_super = -1;
	int64_t id_merge = -1;
	if (super->super < merge->super) {
		id_super = super->super;
		id_merge = merge->super;
	}
	else {
		id_super = merge->super;
		id_merge = super->super;
	}

#if DEVBUILD
	struct cluster * const ref_super = &clusters[id_super];
	struct cluster * const ref_merge = &clusters[id_merge];
	Assert(ref_super->prev == ref_super->id);
	Assert(ref_super->super == ref_super->id);
	Assert(ref_merge->prev == ref_merge->id);
	Assert(ref_merge->super == ref_merge->id);

	// gets the total cluster count prior to the merge to verify the merge code
	int64_t count = 0;
	struct cluster *iter = &clusters[id_super];
	while (iter->next != iter->id) {
		iter = &clusters[iter->next];
		++count;
	}

	iter = &clusters[id_merge];
	while (iter->next != iter->id) {
		iter = &clusters[iter->next];
		++count;
	}

	// while-loops yield the count of the linked-clusters excluding the heads
	int64_t const count_total = 2 + count;
#endif

	super = &clusters[id_super];
	merge = &clusters[id_merge];
	struct cluster *left = super;
	struct cluster *right = merge;
	if (super->id < merge->id) {
		left = super;
		right = merge;
	}
	else {
		left = merge;
		right = super;
	}

	int64_t ref_y = left->y;
	if (left->y < right->y) {
		while (left->y < right->y) {
			left->super = id_super;
			Assert(left->next != left->id);
			left = &clusters[left->next];
		}
		left->super = id_super;
	}
	else if (left->y > right->y) {
		while (left->y > right->y) {
			right->super = id_super;
			Assert(right->next != right->id);
			right = &clusters[right->next];
		}
		right->super = id_super;
	}

	Assert(left->y == right->y);

	if (right->x < left->x) {
		struct cluster *iter = left;
		left = right;
		right = iter;
	}

	// NOTE: clusters are on the same scanline
	ref_y = left->y;
	if (left->prev == left->id) {
		if (right->prev == right->id) {
			struct cluster *prev_left = left;
			while (left->y == ref_y) {
				left->super = id_super;
				if (left->next == left->id) {
					break;
				}
				left = &clusters[left->next];
			}

			left->super = id_super;
			if (left->next == left->id) {
				if (left->y == ref_y) {
					left->next = right->id;
					right->prev = left->id;
					while (right->next != right->id) {
						right->super = id_super;
						right = &clusters[right->next];
					}
					right->super = id_super;
					goto check_merge;
				}
				else {
					prev_left = &clusters[left->prev];
					prev_left->next = right->id;
					right->prev = prev_left->id;
					while (right->y == ref_y) {
						right->super = id_super;
						if (right->next == right->id) {
							break;
						}
						right = &clusters[right->next];
					}

					right->super = id_super;
					if (right->next == right->id) {
						if (right->y == ref_y) {
							right->next = left->id;
							left->prev = right->id;
							goto check_merge;
						}
						else {
							struct cluster *prev_right = &clusters[right->prev];
							prev_right->next = left->id;
							left->prev = prev_right->id;

							left->next = right->id;
							right->prev = left->id;
							goto check_merge;
						}
					}
					else {
						struct cluster *prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						left->next = right->id;
						right->prev = left->id;
						while (right->next != right->id) {
							right->super = id_super;
							right = &clusters[right->next];
						}
						right->super = id_super;
						goto check_merge;
					}
				}
			}
			else {
				prev_left = &clusters[left->prev];
				prev_left->next = right->id;
				right->prev = prev_left->id;
				struct cluster *prev_right = right;
				while (right->y == ref_y) {
					right->super = id_super;
					if (right->next == right->id) {
						break;
					}
					right = &clusters[right->next];
				}

				right->super = id_super;
				if (right->next == right->id) {
					if (right->y == ref_y) {
						right->next = left->id;
						left->prev = right->id;
						while (left->next != left->id) {
							left->super = id_super;
							left = &clusters[left->next];
						}
						left->super = id_super;
						goto check_merge;
					}
					else {
						prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						prev_left = left;
						while (left->y == prev_left->y) {
							left->super = id_super;
							if (left->next == left->id) {
								break;
							}
							left = &clusters[left->next];
						}

						left->super = id_super;
						if (left->next == left->id) {
							if (left->y == prev_left->y) {
								left->next = right->id;
								right->prev = left->id;
								goto check_merge;
							}
							else {
								prev_left = &clusters[left->prev];
								prev_left->next = right->id;
								right->prev = prev_left->id;
								right->next = left->id;
								left->prev = right->id;
								goto check_merge;
							}
						}
						else {
							prev_left = &clusters[left->prev];
							prev_left->next = right->id;
							right->prev = prev_left->id;

							right->next = left->id;
							left->prev = right->id;
							while (left->next != left->id) {
								left->super = id_super;
								left = &clusters[left->next];
							}
							left->super = id_super;
							goto check_merge;
						}
					}
				}
				else {
					// NOTE: after linking these we are ready to repeat the work on the current scanline so nothing more to do in this codeblock
					prev_right = &clusters[right->prev];
					prev_right->next = left->id;
					left->prev = prev_right->id;
				}
			}
		}
		else {
			// NOTE: in this case the `right` has a preceeding scanline
			struct cluster *prev_right = &clusters[right->prev];
			prev_right->next = left->id;
			left->prev = prev_right->id;

			while (left->y == ref_y) {
				left->super = id_super;
				if (left->next == left->id) {
					break;
				}
				left = &clusters[left->next];
			}

			left->super = id_super;
			if (left->next == left->id) {
				if (left->y == ref_y) {
					left->next = right->id;
					right->prev = left->id;
					while (right->next != right->id) {
						right->super = id_super;
						right = &clusters[right->next];
					}
					right->super = id_super;
					goto check_merge;
				}
				else {
					struct cluster *prev_left = &clusters[left->prev];
					prev_left->next = right->id;
					right->prev = prev_left->id;

					// need to link to left which is on the next scanline
					while (right->y == ref_y) {
						right->super = id_super;
						if (right->next == right->id) {
							break;
						}
						right = &clusters[right->next];
					}

					right->super = id_super;
					if (right->next == right->id) {
						if (right->y == ref_y) {
							right->next = left->id;
							left->prev = right->id;
							goto check_merge;
						}
						else {
							prev_right = &clusters[right->prev];
							prev_right->next = left->id;
							left->prev = prev_right->id;

							left->next = right->id;
							right->prev = left->id;
							goto check_merge;
						}
					}
					else {
						prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						left->next = right->id;
						right->prev = left->id;
						while (right->next != right->id) {
							right->super = id_super;
							right= &clusters[right->next];
						}
						right->super = id_super;
						goto check_merge;
					}
				}
			}
			else {
				struct cluster *prev_left = &clusters[left->prev];
				prev_left->next = right->id;
				right->prev = prev_left->id;

				while (right->y == ref_y) {
					right->super = id_super;
					if (right->next == right->id) {
						break;
					}
					right = &clusters[right->next];
				}

				right->super = id_super;
				if (right->next == right->id) {
					if (right->y == ref_y) {
						right->next = left->id;
						left->prev = right->id;
						while (left->next != left->id) {
							left->super = id_super;
							left = &clusters[left->next];
						}
						left->super = id_super;
						goto check_merge;
					}
					else {
						prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						// we need to iterate on left while keeping ourselves on the same scanline to link to the last cluster on right

						prev_left = left;
						while (left->y == prev_left->y) {
							left->super = id_super;
							if (left->next == left->id) {
								break;
							}
							left = &clusters[left->next];
						}

						// then we have to update `super` data member of `left` until we reach the end
						left->super = id_super;
						if (left->next == left->id) {
							if (left->y == prev_left->y) {
								left->next = right->id;
								right->prev = left->id;
								goto check_merge;
							}
							else {
								prev_left = &clusters[left->prev];
								prev_left->next = right->id;
								right->prev = prev_left->id;

								right->next = left->id;
								left->prev = right->id;
								goto check_merge;
							}
						}
						else {
							prev_left = &clusters[left->prev];
							prev_left->next = right->id;
							right->prev = prev_left->id;

							right->next = left->id;
							left->prev = right->id;
							while (left->next != left->id) {
								left->super = id_super;
								left = &clusters[left->next];
							}
							left->super = id_super;
							goto check_merge;
						}
					}
				}
				else {
					// after linking we are ready for executing the merge in a loop
					prev_right = &clusters[right->prev];
					prev_right->next = left->id;
					left->prev = prev_right->id;
				}
			}
		}
	}
	else {
		if (right->prev == right->id) {
			// NOTE: we can confidently skip to the merge loop
		}
		else {
			// complains because this execution path should not happen
			// since we explicitly looked for the first instance where
			// both clusters have the same y-coord values.
			// XCloseDisplay(display);
			Assert(0);
		}
	}

	while (1) {
		Assert(left->y == right->y);

		ref_y = left->y;

		while (left->y == ref_y) {
			left->super = id_super;
			if (left->next == left->id) {
				break;
			}
			left = &clusters[left->next];
		}

		left->super = id_super;
		if (left->next == left->id) {
			if (left->y == ref_y) {
				left->next = right->id;
				right->prev = left->id;
				while (right->next != right->id) {
					right->super = id_super;
					right = &clusters[right->next];
				}
				right->super = id_super;
				goto check_merge;
			}
			else {
				struct cluster *prev_left = &clusters[left->prev];
				prev_left->next = right->id;
				right->prev = prev_left->id;

				// `left` is the last and we need to link to it
				while (right->y == ref_y) {
					right->super = id_super;
					if (right->next == right->id) {
						break;
					}
					right = &clusters[right->next];
				}

				right->super = id_super;
				if (right->next == right->id) {
					if (right->y == ref_y) {
						right->next = left->id;
						left->prev = right->id;
						goto check_merge;
					}
					else {
						struct cluster *prev_right = &clusters[right->prev];
						prev_right->next = left->id;
						left->prev = prev_right->id;

						left->next = right->id;
						right->prev = left->id;
						goto check_merge;
					}
				}
				else {
					struct cluster *prev_right = &clusters[right->prev];
					prev_right->next = left->id;
					left->prev = prev_right->id;

					left->next = right->id;
					right->prev = left->id;

					while (right->next != right->id) {
						right->super = id_super;
						right = &clusters[right->next];
					}
					right->super = id_super;
					goto check_merge;
				}
			}
		}
		else {
			// you need to connect to the right and advance to the next scanline on the right if any
			struct cluster *prev_left = &clusters[left->prev];
			prev_left->next = right->id;
			right->prev = prev_left->id;

			while (right->y == ref_y) {
				right->super = id_super;
				if (right->next == right->id) {
					break;
				}
				right = &clusters[right->next];
			}

			right->super = id_super;
			if (right->next == right->id) {
				if (right->y == ref_y) {
					right->next = left->id;
					left->prev = right->id;
					while (left->next != left->id) {
						left->super = id_super;
						left = &clusters[left->next];
					}
					left->super = id_super;
					goto check_merge;
				}
				else {
					struct cluster *prev_right = &clusters[right->prev];
					prev_right->next = left->id;
					left->prev = prev_right->id;

					prev_left = left;
					// still need to connect to right
					while (left->y == prev_left->y) {
						left->super = id_super;
						if (left->next == left->id) {
							break;
						}
						left = &clusters[left->next];
					}

					left->super = id_super;
					if (left->next == left->id) {
						if (left->y == prev_left->y) {
							left->next = right->id;
							right->prev = left->id;
							goto check_merge;
						}
						else {
							prev_left = &clusters[left->prev];
							prev_left->next = right->id;
							right->prev = prev_left->id;

							right->next = left->id;
							left->prev = right->id;
							goto check_merge;
						}
					}
					else {
						prev_left = &clusters[left->prev];
						prev_left->next = right->id;
						right->prev = prev_left->id;

						right->next = left->id;
						left->prev = right->id;

						while (left->next != left->id) {
							left->super = id_super;
							left = &clusters[left->next];
						}
						left->super = id_super;
						goto check_merge;
					}
				}
			}
			else {
				// links clusters and we are ready for the next iteration
				struct cluster *prev_right = &clusters[right->prev];
				prev_right->next = left->id;
				left->prev = prev_right->id;
			}
		}
	}

	Assert(0);
	//fprintf(stderr, "%s\n", "error: should never execute");
	return;
#if DEVBUILD
check_merge: {
		     // checks the cluster count and we have to initialize to 1 to account for the super-cluster itself
		     count = 1;
		     iter = ref_super;
		     while (iter->next != iter->id) {
			     iter = &clusters[iter->next];
			     ++count;
		     }

		     Assert(count_total == count);

		     count = 1;
		     while (iter->prev != iter->id) {
			     iter = &clusters[iter->prev];
			     ++count;
		     }

		     Assert(count_total == count);

		     iter = ref_super;
		     while (iter->next != iter->id) {
			     Assert(iter->super == id_super);
			     iter = &clusters[iter->next];
		     }

		     iter = ref_super;
		     struct cluster *next = &clusters[iter->next];
		     while (next->next != next->id) {
			     Assert(iter->id < next->id);
			     iter = &clusters[iter->next];
			     next = &clusters[next->next];
		     }

		     return;
	     }
#else
check_merge: {
		     return;
	     }
#endif
}

inline void LinuxSetTimeSpec(
	struct timespec * const clock_time,
	int64_t const nsec
) {
	clock_time->tv_sec  = (nsec / 1000000000);
	clock_time->tv_nsec = (nsec % 1000000000);
}

inline void LinuxSetDelayTime(
        struct timespec * const clock_target,
        struct timespec const * const clock_start,
        struct timespec const * const clock_delta
) {
	clock_target->tv_sec = (
		(clock_start->tv_sec + clock_delta->tv_sec) +
		((clock_start->tv_nsec + clock_delta->tv_nsec) / 1000000000)
	);
	clock_target->tv_nsec = (
		((clock_start->tv_nsec + clock_delta->tv_nsec) % 1000000000)
	);
}

inline void LinuxDiffTimeSpec(
	struct timespec * const clock_delta,
	struct timespec const * const clock_start,
	struct timespec const * const clock_end
) {
	int64_t nsec_diff = 0;
	int64_t const nsec_start = 1000000000 * clock_start->tv_sec + clock_start->tv_nsec;
	int64_t const nsec_end   = 1000000000 *   clock_end->tv_sec +   clock_end->tv_nsec;
	if (nsec_end > nsec_start) {
		nsec_diff = (nsec_end - nsec_start);
	} else {
		nsec_diff = (nsec_start - nsec_end);
	}
	clock_delta->tv_sec  = (nsec_diff / 1000000000);
	clock_delta->tv_nsec = (nsec_diff % 1000000000);
}

inline void LinuxDelay(
        clockid_t clock_id,
        struct timespec const * const clock_target
) {
        int rc = 0;
        Assert(CLOCK_MONOTONIC == clock_id);
        do {
                rc = clock_nanosleep(clock_id, TIMER_ABSTIME, clock_target, NULL);
                Assert(EFAULT != rc);
                Assert(EINVAL != rc);
        } while (EINTR == rc);
}

extern "C" void* EngineInit(void)
{
	errno = 0;
	int rc = 0;
	rc = sysconf(_SC_PAGESIZE);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	int64_t const pagesz = rc;
	int64_t const mask_page = (pagesz - 1);

	Display *display = XOpenDisplay(NULL);
	if (!display) {
		fprintf(stderr, "%s\n", "error: failed to open display");
		_exit(1);
	}

	Screen *screen = DefaultScreenOfDisplay(display);
	int32_t const width_screen = WidthOfScreen(screen);
	int32_t const height_screen = HeightOfScreen(screen);
	int64_t const pixels_screen = (width_screen * height_screen);
	// NOTE: assuming 32-bit depth for a pixel, even if the visual depth is 24-bits the XImage data can still have a 32-bit depth and this is what we are counting on
	int32_t const depth_pixel = 32;
	// NOTE: assuming that the scanline bytes are exactly width_screen x depth_pixel, at least that has been my experience so far with XImages; if not the case, we can know that after getting the XImage data
	int64_t const bytes_screen = depth_pixel * pixels_screen;
	int64_t bytes_partition = bytes_screen;
	struct cluster stud = {};
	struct cluster *clustep = &stud;
	int64_t bytes_clusters = pixels_screen * sizeof(*clustep);
	int64_t bytes_cluster_list = pixels_screen * sizeof(CID);
	int64_t bytes_required = (
		pagesz +
		bytes_screen +
		bytes_screen +
		bytes_partition +
		bytes_clusters +
		bytes_cluster_list +
		0
	);
	int64_t bytes_aligned = ((bytes_required + mask_page) & (~mask_page));
	int64_t bytes_mmap = (bytes_aligned << 1);

	errno = 0;
	void *base = mmap(NULL, bytes_mmap, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (MAP_FAILED == base) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

        errno = 0;
	rc = madvise(base, bytes_mmap, MADV_WILLNEED);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
		_exit(1);
	}

	Window root = DefaultRootWindow(display);
	Cursor cursor = XCreateFontCursor(display, XC_crosshair);
	rc = XGrabPointer(
		display,
		root,
		False,
		ButtonPressMask | ButtonReleaseMask,
		GrabModeSync,
		GrabModeAsync,
		root,
		cursor,
		CurrentTime
	);

	if (GrabSuccess != rc) {
		fprintf(stderr, "%s\n", "error: failed to grab pointer");
		XFreeCursor(display, cursor);
		XCloseDisplay(display);
		_exit(1);
	}

	XEvent ev = {};
	Window subwindow = 0;
	// NOTE: the game is not played with a mouse so that we know that we don't need to worry about previous button events unlike `xwininfo` because of its general purpose
	while (0 == subwindow) {
		XAllowEvents(display, SyncPointer, CurrentTime);
		while (XPending(display)) {
			XNextEvent(display, &ev);
			if (ButtonPress == ev.type) {
				if (0 == subwindow) {
					subwindow = ev.xbutton.subwindow;
					if (0 == subwindow) {
						subwindow = root;
					}
				}
				break;
			}
		}
	}

	XUngrabPointer(display, CurrentTime);

	int unsigned nchildren_return = 0;
	Window root_return = 0;
	Window parent_return = 0;
	Window *children_return = NULL;
	rc = XQueryTree(
		display,
		subwindow,
		&root_return,
		&parent_return,
		&children_return,
		&nchildren_return
	);

	if (!rc) {
		fprintf(stderr, "%s\n", "error: failed to query tree");
		XFreeCursor(display, cursor);
		XCloseDisplay(display);
		_exit(1);
	}
	else if (!children_return) {
		fprintf(stderr, "%s\n", "error: picked window with no children");
		XFreeCursor(display, cursor);
		XCloseDisplay(display);
		_exit(1);
	}

	Window GameWindow = 0;
	if (1 == nchildren_return) {
		GameWindow = children_return[0];
	}

	if (!GameWindow) {
		fprintf(stderr, "%s\n", "error: failed to get window");
		XFree(children_return);
		XFreeCursor(display, cursor);
		XCloseDisplay(display);
		_exit(1);
	}

	XFree(children_return);
	XFreeCursor(display, cursor);

	XWindowAttributes attributes = {};
	XGetWindowAttributes(display, GameWindow, &attributes);
	int64_t const width = attributes.width;
	int64_t const height = attributes.height;
	int64_t const depth_window = attributes.depth;
	Visual *visual = attributes.visual;

	int32_t iters = 0;
	int32_t red_shift = 0;
	int32_t green_shift = 0;
	int32_t blue_shift = 0;
	int32_t const rgb_mask = 0xff;
	int32_t const red_mask = visual->red_mask;
	int32_t const green_mask = visual->green_mask;
	int32_t const blue_mask = visual->blue_mask;
	while ((rgb_mask << red_shift) != red_mask) {
		red_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	iters = 0;
	while ((rgb_mask << green_shift) != green_mask) {
		green_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	iters = 0;
	while ((rgb_mask << blue_shift) != blue_mask) {
		blue_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	// FIXME: disable fullscreen toggling because the client might support this but we are enforcing a fixed sized window. This one is a tough one because there's no way to remove the internal atoms that establish the communication of the game with the Window Manager, which is the original approach I was considering.
	XSizeHints *SizeHintsGameWindow = XAllocSizeHints();
	if (!SizeHintsGameWindow) {
		XCloseDisplay(display);
		_exit(1);
	}

	// NOTES: fixes the game window dimensions so that we can do our work without defensive programming for handling dimension changes; in practice we don't want to change the game dimensions when we call this engine and so this guarantees that
	SizeHintsGameWindow->flags = (PMinSize | PMaxSize);
	SizeHintsGameWindow->min_width = width;
	SizeHintsGameWindow->max_width = width;
	SizeHintsGameWindow->min_height = height;
	SizeHintsGameWindow->max_height = height;
	XSetWMNormalHints(display, GameWindow, SizeHintsGameWindow);
	XSync(display, False);

	struct map *data = (typeof(data)) base;
	XImage *GameImage = XShmCreateImage(
		display,
		visual,
		depth_window,
		ZPixmap,
		NULL,
		&data->shminfo,
		width,
		height
	);

	if (!GameImage) {
		XFree(SizeHintsGameWindow);
		XCloseDisplay(display);
		fprintf(stderr, "%s\n", "error: XShmCreateImage failed");
		_exit(1);
	}

	int64_t const bytes_per_pixel = (depth_pixel >> 3);
	int64_t const pitch = bytes_per_pixel * width;
	int64_t const pixels = (width * height);
	int64_t const bytes_backbuffer = (bytes_per_pixel * pixels);
	int64_t const bytes_framebuffer = (bytes_per_pixel * pixels);
	if (GameImage->bytes_per_line != pitch) {
		fprintf(stderr, "%s\n", "error: scanline length mismatch");
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}
	else if ((GameImage->bytes_per_line * GameImage->height) != bytes_framebuffer) {
		fprintf(stderr, "%s\n", "error: framebuffer size mismatch");
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	rc = shmget(
		IPC_PRIVATE,
		bytes_framebuffer,
		IPC_CREAT | 0777
	);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to get shared-memory identifier");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}

	data->shminfo.shmid = rc;
	bytes_partition = bytes_framebuffer;
	bytes_clusters = pixels * sizeof(*clustep);
	bytes_cluster_list = pixels * sizeof(CID);
	// NOTE: the first page is reserved for the `struct map` after that we can do whatever we want but we opted to ensure 64-byte alignment of the clusters and cluster_list arrays
	int64_t const offset_partition = pagesz;
	int64_t const offset_clusters = (
		(((offset_partition + bytes_partition) + 0x3fL) & (~0x3fL))
	);
	int64_t const offset_cluster_list = (
		(((offset_clusters + bytes_clusters) + 0x3fL) & (~0x3fL))
	);
	int64_t const offset_backbuffer = (
		(((offset_cluster_list + bytes_cluster_list) + 0x3fL) & (~0x3fL))
	);
	// NOTE: `shmat` requires the framebuffer address to be paged aligned
	int64_t const offset_framebuffer = (
		(((offset_backbuffer + bytes_backbuffer) + mask_page) & (~mask_page))
	);

	void *framebuffer = ((char*) base) + offset_framebuffer;
	if (((uintptr_t) framebuffer) & mask_page) {
		fprintf(stderr, "%s\n", "error: framebuffer not paged aligned");
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	data->shminfo.shmaddr = GameImage->data = ((char*) shmat(data->shminfo.shmid, framebuffer, SHM_REMAP));
	if (data->shminfo.shmaddr != framebuffer) {
		fprintf(stderr, "%s\n", "error: shmat changed the framebuffer address");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}

	data->shminfo.readOnly = False;
	if (!XShmAttach(display, &data->shminfo)) {
		fprintf(stderr, "%s\n", "error: XShmAttach failed");
		shmdt(data->shminfo.shmaddr);
		shmctl(data->shminfo.shmid, IPC_RMID, 0);
		// NOTE: framebuffer data is not heap allocated and so we must nullify it for XDestroyImage otherwise it will attempt to free a memory mapped region
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		XFree(SizeHintsGameWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	uint64_t const plane_mask = 0xffffff;
	if (!XShmGetImage(display, GameWindow, GameImage, 0, 0, plane_mask)) {
		fprintf(stderr, "%s\n", "error: XShmGetImage failed");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XShmDetach(display, &data->shminfo);
		shmdt(data->shminfo.shmaddr);
		shmctl(data->shminfo.shmid, IPC_RMID, 0);
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		XFree(SizeHintsGameWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	XSetWindowAttributes OutputWindowAttributes = {};
	OutputWindowAttributes.background_pixel = BlackPixelOfScreen(screen);
	OutputWindowAttributes.event_mask = (
		ExposureMask |
		KeyPressMask |
		0
	);

	Window OutputWindow = XCreateWindow(
		display,
		DefaultRootWindow(display),
		0,
		0,
		width,
		height,
		0,
		depth_window,
		InputOutput,
		DefaultVisualOfScreen(screen),
		CWBackPixel | CWEventMask,
		&OutputWindowAttributes
	);

	// TODO: consider enabling full screen toggling of the output window for the demo
	XSizeHints *SizeHints = XAllocSizeHints();
	if (!SizeHints) {
		fprintf(stderr, "%s\n", "error; XSizeHints allocation failed");
		XShmDetach(display, &data->shminfo);
		shmdt(data->shminfo.shmaddr);
		shmctl(data->shminfo.shmid, IPC_RMID, 0);
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		XFree(SizeHintsGameWindow);
		XDestroyWindow(display, OutputWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	SizeHints->flags = PMinSize;
	SizeHints->min_width = width;
	SizeHints->min_height = height;
	XSetWMNormalHints(display, OutputWindow, SizeHints);
	XStoreName(display, OutputWindow, "Handcrafted Blue Computer Vision Engine");

	XMapWindow(display, OutputWindow);
	XWindowEvent(display, OutputWindow, ExposureMask, &ev);

	char *backbuffer = (((char*) base) + offset_backbuffer);
	XImage *OutputImage = XCreateImage(
		display,
		DefaultVisualOfScreen(DefaultScreenOfDisplay(display)),
		depth_window,
		ZPixmap,
		0,
		backbuffer,
		width,
		height,
		depth_pixel,
		0
	);

	if (!OutputImage) {
		fprintf(stderr, "%s\n", "error: XCreateImage failed");
		XShmDetach(display, &data->shminfo);
		shmdt(data->shminfo.shmaddr);
		shmctl(data->shminfo.shmid, IPC_RMID, 0);
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		XFree(SizeHintsGameWindow);
		XFree(SizeHints);
		XDestroyWindow(display, OutputWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	if (
		(GameImage->width != OutputImage->width) ||
		(GameImage->height != OutputImage->height) ||
		(GameImage->format != OutputImage->format) ||
		(GameImage->depth != OutputImage->depth) ||
		(GameImage->red_mask != OutputImage->red_mask) ||
		(GameImage->green_mask != OutputImage->green_mask) ||
		(GameImage->blue_mask != OutputImage->blue_mask) ||
		(GameImage->bitmap_pad != OutputImage->bitmap_pad) ||
		(GameImage->bitmap_bit_order != OutputImage->bitmap_bit_order) ||
		(GameImage->bytes_per_line != OutputImage->bytes_per_line) ||
		(GameImage->bits_per_pixel != OutputImage->bits_per_pixel) ||
		0
	   ) {
		fprintf(stderr, "%s\n", "error: surprising XImage mistmatch");
		XShmDetach(display, &data->shminfo);
		shmdt(data->shminfo.shmaddr);
		shmctl(data->shminfo.shmid, IPC_RMID, 0);
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		OutputImage->data = NULL;
		XDestroyImage(OutputImage);
		XFree(SizeHintsGameWindow);
		XFree(SizeHints);
		XDestroyWindow(display, OutputWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	int32_t running = 1;
	int32_t frameno = 0;
	data->display = display;
	data->screen = screen;
	data->GameWindow = GameWindow;
	data->OutputWindow = OutputWindow;
	data->GameImage = GameImage;
	data->OutputImage = OutputImage;
	data->SizeHintsGameWindow = SizeHintsGameWindow;
	data->SizeHints = SizeHints;
	data->running = running;
	data->frameno = frameno;
	data->bytes_partition = bytes_partition;
	data->bytes_clusters = bytes_clusters;
	data->bytes_cluster_list = bytes_cluster_list;
	data->bytes_backbuffer = bytes_backbuffer;
	data->bytes_framebuffer = bytes_framebuffer;
	data->offset_partition = offset_partition;
	data->offset_clusters = offset_clusters;
	data->offset_cluster_list = offset_cluster_list;
	data->offset_backbuffer = offset_backbuffer;
	data->offset_framebuffer = offset_framebuffer;
	data->pixels = pixels;
	data->width = width;
	data->height = height;
	data->pitch = pitch;
	data->red_shift = red_shift;
	data->green_shift = green_shift;
	data->blue_shift = blue_shift;
	float constexpr FPSFloat = ENGINE_FPS_TARGET;
	float constexpr FPSInvFloat = 1.0e9f / FPSFloat;
	int64_t constexpr FrameDurationTargetNanoSec = FPSInvFloat;
	LinuxSetTimeSpec(&data->time_target, FrameDurationTargetNanoSec);
	fprintf(stdout, "GameWindow: %d\n", data->GameWindow);
	return base;
}

extern "C" void EngineFree(void *base)
{
	if (!base) {
		fprintf(stderr, "%s\n", "error: NULL pointer error");
		_exit(1);
	}	
	struct map *data = (typeof(data)) base;
	XShmDetach(data->display, &data->shminfo);
	shmdt(data->shminfo.shmaddr);
	shmctl(data->shminfo.shmid, IPC_RMID, 0);
	data->GameImage->data = NULL;
	XDestroyImage(data->GameImage);
	data->OutputImage->data = NULL;
	XDestroyImage(data->OutputImage);
	XFree(data->SizeHintsGameWindow);
	XFree(data->SizeHints);
	XDestroyWindow(data->display, data->OutputWindow);
	XCloseDisplay(data->display);
}

extern "C" int EngineUpdateAndRender(void *base)
{
	int rc = 1;
	XEvent ev = {};
	struct map *priv = (typeof(priv)) base;
	Display *display = priv->display;
	Screen *screen = priv->screen;
	XImage *GameImage = priv->GameImage;
	XImage *OutputImage = priv->OutputImage;
	Window GameWindow = priv->GameWindow;
	Window OutputWindow = priv->OutputWindow;
	Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
	Atom fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
	if (XCheckTypedWindowEvent(display, OutputWindow, KeyPress, &ev)) {
		if ((KBD_ESC == ev.xkey.keycode)) {
			rc = priv->running = 0;
			fprintf(stdout, "%s\n", "quitting upon user request");
			return rc;
		} else if ((KBD_F11 == ev.xkey.keycode)) {
			XEvent FullscreenToggleEvent = {};
			FullscreenToggleEvent.type = ClientMessage;
			FullscreenToggleEvent.xclient.window = OutputWindow;
			FullscreenToggleEvent.xclient.message_type = wm_state;
			FullscreenToggleEvent.xclient.format = 32;
			FullscreenToggleEvent.xclient.data.l[0] = _NET_WM_STATE_TOGGLE;
			FullscreenToggleEvent.xclient.data.l[1] = fullscreen;
			FullscreenToggleEvent.xclient.data.l[2] = 0;
			XSendEvent(
				display,
				DefaultRootWindow(display),
				False,
				SubstructureRedirectMask | SubstructureNotifyMask,
				&FullscreenToggleEvent
			);
		}
	}

	uint64_t const plane_mask = 0xffffff;
	XShmGetImage(display, GameWindow, GameImage, 0, 0, plane_mask);

	int64_t const bytes_partition = priv->bytes_partition;
	int64_t const bytes_clusters = priv->bytes_clusters;
	int64_t const bytes_cluster_list = priv->bytes_cluster_list;
	int64_t const bytes_backbuffer = priv->bytes_backbuffer;
	int64_t const offset_partition = priv->offset_partition;
	int64_t const offset_clusters = priv->offset_clusters;
	int64_t const offset_cluster_list = priv->offset_cluster_list;
	int64_t const offset_backbuffer = priv->offset_backbuffer;
	OutputImage->data = (typeof(OutputImage->data)) (((char*) base) + offset_backbuffer);
	struct cluster *clusters = (typeof(clusters)) (((char*) base) + offset_clusters);
	Assert(0 == (((uintptr_t) clusters) & 63));

	char *data_framebuffer = GameImage->data;
	int32_t const red_mask = GameImage->red_mask;
	int32_t const green_mask = GameImage->green_mask;
	int32_t const blue_mask = GameImage->blue_mask;
	int32_t const red_shift = priv->red_shift;
	int32_t const green_shift = priv->green_shift;
	int32_t const blue_shift = priv->blue_shift;
	int32_t const width = priv->width;
	int32_t const height = priv->height;
	int32_t const pitch = priv->pitch;

	int32_t *part = (typeof(part)) (((char*) base) + offset_partition);
	Assert(0 == (((uintptr_t) part) & 63));

	memset(part, 0xff, bytes_partition);
	data_framebuffer = GameImage->data;
	for (int64_t y = 0; y != height; ++y) {
		int32_t *frame = (int32_t*) data_framebuffer;
		for (int64_t x = 0; x != width; ++x) {
			int32_t rc = Clustering(
					part,
					frame,
					red_mask,
					green_mask,
					blue_mask,
					red_shift,
					green_shift,
					blue_shift,
					width,
					x,
					y
				       );
			Assert(-1 != rc);
		}
		data_framebuffer += pitch;
	}

	int64_t clno = 0;
	CID *cl = (typeof(cl)) (((char*) base) + offset_cluster_list);
	Assert(0 == (((uintptr_t) cl) & 63));
	memset(cl, 0, bytes_cluster_list);

	data_framebuffer = GameImage->data;
	int32_t *framebuffer = (typeof(framebuffer)) data_framebuffer;
	// links nodes of constant y-striped clusters (same scanline)
	int64_t const pixels = priv->pixels;
	for (int64_t i = 0; i != pixels; ++i) {
		struct cluster *cluster = &clusters[i];
		int32_t const rgb = framebuffer[i];
		int32_t const r = ((red_mask & rgb) >> red_shift);
		int32_t const g = ((green_mask & rgb) >> green_shift);
		int32_t const b = ((blue_mask & rgb) >> blue_shift);
		int32_t const y = (i / width);
		int32_t const x = i - (width * y);
		if (Blue(r, g, b) && (part[i] < 0)) {
			cluster->root = i;
			cluster->node = i;
			cluster->prev = i;
			cluster->next = i;
			cluster->size = -(part[i]);
			cluster->super = -1;
			cluster->total = 1;
			cluster->id = i;
			cluster->mask = BLUE_MASK_SONIC;
			cluster->x = x;
			cluster->y = y;
			int64_t const childno = (cluster->size - 1);
			cluster->node = (i + childno);
			for (int64_t j = 0; j != childno; ++j) {
				int64_t id = ((i + 1) + (childno - 1) - j);
				int32_t const y = (id / width);
				int32_t const x = id - (width * y);
				Assert(part[id] == i);
				struct cluster *child = &clusters[id];
				child->root = i;
				child->node = (id - 1);
				child->prev = id;
				child->next = id;
				child->size = 0;
				child->super = -1;
				child->total = 1;
				child->id = id;
				child->mask = BLUE_MASK_SONIC;
				child->x = x;
				child->y = y;
			}
			cl[clno] = i;
			++clno;
		}
	}

	if (clno > 2) {
		for (int64_t i = 0; i != (clno - 1); ++i) {
			int64_t const ii = cl[i];
			struct cluster *curr = &clusters[ii];
			Assert(curr->root == curr->id);
			Assert(BLUE_MASK_SONIC == curr->mask);

			int64_t const x_l = curr->x;
			int64_t x_u = curr->x;
			if (
				(curr->next != curr->id) &&
				(curr->y == clusters[curr->next].y)
			   ) {
				struct cluster const *iter = &clusters[curr->next];
				struct cluster const *prev = &clusters[curr->next];
				while ((iter->y == curr->y) && (iter->next != iter->id)) {
					Assert(BLUE_MASK_SONIC == iter->mask);
					prev = iter;
					iter = &clusters[iter->next];
				}

				if (iter->y != curr->y) {
					iter = prev;
				}

				Assert(iter->y == curr->y);

				if (1 == iter->size) {
					x_u = iter->x;
				}
				else {
					iter = &clusters[iter->node];
					Assert(iter->root != iter->id);
					x_u = iter->x;
				}
			}
			else if (1 == curr->size) {
				continue;
			}
			else if (curr->size > 1) {
				// NOTE using the redundant logic expression for readability
				struct cluster const * const node = &clusters[curr->node];
				Assert(BLUE_MASK_SONIC == node->mask);
				x_u = node->x;
			}

			Assert(x_l != x_u);
			Assert(x_l < x_u);

			// initially marks ordinary clusters into super-clusters
			struct cluster *iter = &clusters[curr->id];
			if (-1 == iter->super) {
				while (iter->prev != iter->id) {
					iter = &clusters[iter->prev];
				}
				iter->super = iter->id;
			}
			else {
				iter = &clusters[iter->super];
				Assert(iter->prev == iter->id);
				Assert(iter->super == iter->id);
			}

			int64_t super = iter->super;
			for (int64_t j = (i + 1); j != clno; ++j) {
				int64_t const jj = cl[j];
				struct cluster *next = &clusters[jj];
				Assert(0 != next->size);
				Assert(next->root == next->id);
				if (next->y == curr->y) {
					continue;
				}
				else if ((next->y - curr->y) > 1) {
					break;
				}
				else if (next->root != next->id) {
					continue;
				}
				else if (next->super == super) {
					continue;
				}
				else if ((next->super != -1) && (next->super != super)) {
					MergeSuperClusters(
						curr,
						next,
						clusters,
						super,
						x_l,
						x_u
					);
					if (super != curr->super) {
						Assert(curr->super == next->super);
						super = curr->super;
					}
					continue;
				}

				CheckBoundsAndMerge(
					curr,
					next,
					clusters,
					super,
					x_l,
					x_u
				);
			}
		}

		int64_t total_max = 1;
		int64_t id_max = -1;
		// gets the id of the largest blob of pixels that characterize the player
		for (int64_t i = 0; i != clno; ++i) {
			int64_t id = cl[i];
			struct cluster *iter = &clusters[id];
			if (-1 == iter->super) {
				continue;
			}
			struct cluster *super = &clusters[iter->super];
			Assert(super->super == super->id);
			Assert(super->prev == super->id);

			iter = super;
			int64_t count = 0;
			do {
				count += iter->size;
				iter = &clusters[iter->next];
			} while (iter->next != iter->id);
			super->total = count;

			if (super->total > total_max) {
				id_max = super->id;
				total_max = super->total;
			}
		}

		if (-1 != id_max) {
			// obtains the limits of the player-bounding rectangle
			struct cluster *c = &clusters[id_max];
			int64_t x_min = width;
			int64_t x_max = 0;
			int64_t y_min = height;
			int64_t y_max = 0;
			struct cluster *iter = c;
			while (iter->next != iter->id) {
				struct cluster const * const node = &clusters[iter->node];
				if (iter->x < x_min) {
					x_min = iter->x;
				}
				if (iter->y < y_min) {
					y_min = iter->y;
				}
				if (node->x > x_max) {
					x_max = node->x;
				}
				if (iter->y > y_max) {
					y_max = iter->y;
				}
				iter = &clusters[iter->next];
			}
			c->x_min = x_min;
			c->x_max = x_max;
			c->y_min = y_min;
			c->y_max = y_max;

			// PERF: clears the player-bounding region before updating the backbuffer instead of clearing the entire window
			char *data_backbuffer = (typeof(data_backbuffer)) (((char*) base) + offset_backbuffer + (y_min * pitch));
			for (int32_t y = y_min; y != y_max; ++y) {
				int32_t *frame = (typeof(frame)) data_backbuffer;
				for (int32_t x = x_min; x != x_max; ++x) {
					frame[x] ^= frame[x];
				}
				data_backbuffer += pitch;
			}

			iter = c;
			data_backbuffer = (typeof(data_backbuffer)) (((char*) base) + offset_backbuffer);
			int32_t *frame = (typeof(frame)) data_backbuffer;
			while (iter->next != iter->id) {
				for (int64_t i = 0; i != iter->size; ++i) {
					int64_t const ii = (i + iter->id);
					struct cluster const * const node = &clusters[ii];
					int64_t const x = node->x;
					int64_t const y = node->y;
					int64_t const id = width * y + x;
					int32_t const rgb = (0xff << green_shift);
					frame[id] = rgb;
				}
				iter = &clusters[iter->next];
			}

			XClearWindow(display, OutputWindow);
			XPutImage(
				display,
				OutputWindow,
				DefaultGCOfScreen(screen),
				OutputImage,
				c->x_min,
				c->y_min,
				c->x_min,
				c->y_min,
				(c->x_max - c->x_min),
				(c->y_max - c->y_min)
			);
			XFlush(display);
		}
		else {
			XClearWindow(display, OutputWindow);
			XFlush(display);
		}
	}
	else {
		XClearWindow(display, OutputWindow);
		XFlush(display);
	}

	return rc;
}

extern "C" void EngineTime(void *base)
{
	struct map *data = (typeof(data)) base;
	clock_gettime(CLOCK_MONOTONIC, &data->time_start);
}

#if DEVBUILD
extern "C" void EngineDelay(void *base)
{
	struct map *data = (typeof(data)) base;
	LinuxSetDelayTime(&data->time_iddle, &data->time_start, &data->time_target);
	LinuxDelay(CLOCK_MONOTONIC, &data->time_iddle);
	if (64 == data->frameno) {
		data->frameno = 0;
		struct timespec time_delta = {};
		struct timespec time_end = {};
		clock_gettime(CLOCK_MONOTONIC, &time_end);
		LinuxDiffTimeSpec(&time_delta, &data->time_start, &time_end);
		float const etime = (
			1.0e+3 * time_delta.tv_sec +
			1.0e-6 * time_delta.tv_nsec
		);
		float const FPS = 1.0e+3f / etime;
		fprintf(stdout, "\nFPS: %.1f\netime (ms): %.1f\n", FPS, etime);
	}
	else {
		data->frameno++;
	}
}
#else
extern "C" void EngineDelay(void *base)
{
	struct map *data = (typeof(data)) base;
	LinuxSetDelayTime(&data->time_iddle, &data->time_start, &data->time_target);
	LinuxDelay(CLOCK_MONOTONIC, &data->time_iddle);
}
#endif
