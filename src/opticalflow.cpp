#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <sstream>
#ifdef _OPENMP
#include <pthread.h>
#include <omp.h>
#endif
#include "image.hpp"
#include "misc.hpp"
using namespace std;

#define PROGRAM "opticalflow"
#define VERSION_NUMBER "0.2.5"
#define RELEASE_DATE "(2011/02/05)"

#ifdef _OPENMP
#define VERSION_CODE0 VERSION_NUMBER "-openmp"
#else
#define VERSION_CODE0 VERSION_NUMBER
#endif

#ifdef GRAYSCALE
#define VERSION_CODE1 VERSION_CODE0 "-grayscale"
#else
#define VERSION_CODE1 VERSION_CODE0
#endif


#define VERSION VERSION_CODE1

////////////////////////////////////////////////////////////////////////////////
// DEFAULT CONFIGURATION
////////////////////////////////////////////////////////////////////////////////
//#define BILINEAR
//#define GAUSIANM
#define GAUSIAN
#define LINEAR

const int DIRECTION = 5;
const int DETAIL = 1;

////////////////////////////////////////////////////////////////////////////////
// PROGRAM SETTINGS
////////////////////////////////////////////////////////////////////////////////
#ifdef _OPENMP
#ifndef THREAD_NUM
#define THREAD_NUM 5
#endif
#else
#define THREAD_NUM 1
#endif

typedef long long error;

vector<string> file_names;
bool prediction_mode;
bool verbose_mode;
bool quiet_mode;
string output_file;
int block_size;
double delay_ratio;
double blockmatching_ratio;
string flow_file;
pixel ignore_color;
pixel undefined_color;
double alternation_ratio;
string convert_command;
double target_x, target_y, target_z;


// Comparison of two pixels
#ifdef GRAYSCALE
inline error pixel_diff(pixel a, pixel b) {
	int c = (a & 255) - (b & 255);
	return c * c;
}
#else
inline error pixel_diff(pixel a, pixel b) {
	unsigned char *ca = (unsigned char *)&a, *cb = (unsigned char *)&b;
	return ((int)ca[0] - (int)cb[0]) * ((int)ca[0] - (int)cb[0]) +
	 ((int)ca[1] - (int)cb[1]) * ((int)ca[1] - (int)cb[1]) +
	 ((int)ca[2] - (int)cb[2]) * ((int)ca[2] - (int)cb[2]) +
	 ((int)ca[3] - (int)cb[3]) * ((int)ca[3] - (int)cb[3]);
}
#endif

////////////////////////////////////////////////////////////////////////////////
// WEIGHT FUNCTIONS
////////////////////////////////////////////////////////////////////////////////
inline double hanning(double x) {
	if (x < 0.0) x = -x;
	// if (x < 1.0) return 0.5 - 0.5 * cos(8 * atan(1) * x);
	if (x < 1.0) return 1.0;
	// if (x < 1.0) return 1 - x;
	return 0.0;
}

inline double hanning_sqrt(double x) {
	return hanning(sqrt(x));
}

inline double bilinear(double x) {
	if (x < 0) x = -x;
	if (x < 1.0) return 1 - x;
	return 0.0;
}

inline double bilinear_sqrt(double x) {
	return bilinear(sqrt(x));
}

inline double bicubic(double x) {
	if (x < 0) x = -x;
	if (x < 1.0) return 1 - 2 * x * x + x * x * x;
	if (x < 2.0) return 4 - 8 * x + 5 * x * x - x * x * x;
	return 0.0;
}

inline double bicubic_sqrt(double x) {
	return bicubic(sqrt(x));
}

inline double gausian(double x) {
	if (3.0 < x) return 0.0;
	return exp(-x * x / 2);
//	return hanning(x);
}

inline double gausian_sqrt(double x) {
	return gausian(sqrt(x));
}

#define FAST_RESOLUTION 4096
#define FAST(func, upper) \
inline double func##_fast(double x) {\
	static int init = 0;\
	static double cache[(int)((upper) * (FAST_RESOLUTION)) + 1];\
	if (!init) {\
		init = 1;\
		for (int i = 0; i <= (int)((upper) * (FAST_RESOLUTION)); i++)\
		  cache[i] = func(i / (double)(FAST_RESOLUTION));\
	}\
	if (x < 0.0) x = -x;\
	if (x < upper) {\
		double xx = x * FAST_RESOLUTION;\
		return cache[(int)xx] * (1 - xx + (int)xx) +\
		 cache[1 + (int)xx] * (xx - (int)xx);\
	}\
	return 0.0;\
}

// FAST(hanning, 1)
FAST(hanning_sqrt, 1)
// FAST(bilinear, 1)
FAST(bilinear_sqrt, 1)
// FAST(bicubic, 2)
FAST(bicubic_sqrt, 4)
// FAST(gausian, 3)
FAST(gausian_sqrt, 9)

////////////////////////////////////////////////////////////////////////////////
// OPTICAL FLOW CLASS
////////////////////////////////////////////////////////////////////////////////
template<class element> struct optical_flow {
	int width, height;
	int number_of_images;
	image<element> *source_images, *images;
	image<coordinate> flow[3], flow_summary[3];
#ifdef OPTIMIZE
	image<int> *image_change;
#endif
	
	template<int dimension> void evaluation(
	 image<error> *result, image<int> *weight, double distance) {
		profile("Evaluation");
#ifndef NDEBUG
		for (int i = 0; i < DIRECTION; i++) {
			assert(result[i].width == width && result[i].height == height);
			assert(weight[i].width == width && weight[i].height == height);
			for (int y = 0; y < height; y++)
			 for (int x = 0; x < width; x++) {
				assert(result[i](x, y) == error());
				assert(weight[i](x, y) == 0);
			}
		}
#endif
		assert(images[0].width == width && images[0].height == height);
#ifdef OPTIMIZE
		assert(image_change[0].width == width);
		assert(image_change[0].height == height);
#endif
#ifdef _OPENMP
#pragma omp parallel for
#endif
		for (int dir = 0; dir < DIRECTION; dir++) {
			for (int id = 1; id < number_of_images; id++) {
				assert(images[id].width == width * DETAIL);
				assert(images[id].height == height * DETAIL);
				int d = (int)round(pow(id, dimension) * distance * DETAIL);
				int dx[] = {0, -d, d, 0, 0, d, d, -d, -d},
				 dy[] = {0, 0, 0, -d, d, d, -d, d, -d};
				int fx = 0, tx = width;
#ifdef OPTIMIZE
				assert(image_change[id].width == width * DETAIL);
				assert(image_change[id].height == height * DETAIL);
				int xx_cache[width];
				fx = width; tx = 0;
				for (int x = 0; x < width; x++) {
					int xx = x * DETAIL - dx[dir] + DETAIL / 2;
					xx_cache[x] = xx;
					if (xx < 0 || width * DETAIL <= xx) continue;
					fx = min(x, fx); tx = max(x, tx);
				}
				tx++;
#ifndef NDEBUG
				for (int x = 0; x < width; x++) {
					int xx = xx_cache[x];
					if (xx < 0 || width * DETAIL <= xx)
					 assert(x < fx || tx <= x);
					else assert(fx <= x && x < tx);
				}
#endif
#endif
				for (int y = 0; y < height; y++) {
					int yy = y * DETAIL - dy[dir] + DETAIL / 2;
					if (yy < 0 || height * DETAIL <= yy) continue;
					for (int x = fx; x < tx; x++) {
						int present_pixel = images[0](x, y);
						if (present_pixel == ignore_color) {
#ifdef OPTIMIZE
							x = image_change[0](x, y) - 1;
#endif
							continue;
						}
#ifdef OPTIMIZE
						int xx = xx_cache[x];
						assert(xx == x * DETAIL - dx[dir] + DETAIL / 2);
						assert(0 <= xx && xx < width * DETAIL);
#else
						int xx = x * DETAIL - dx[dir] + DETAIL / 2;
						if (xx < 0 || width * DETAIL <= xx) continue;
#endif
						int previous_pixel = images[id](xx, yy);
						if (previous_pixel == ignore_color) {
#ifdef OPTIMIZE
							x += (image_change[id](xx, yy) - xx) / DETAIL - 1;
#endif
							continue;
						}
#ifdef OPTIMIZE
						int fxx = x, txx = min(image_change[0](x, y),
						 x + image_change[id](xx, yy) - xx);
						error diff = pixel_diff(previous_pixel, present_pixel);
						for (; x < txx; x++) weight[dir](x, y)++;
						if (present_pixel != previous_pixel)
						 for (x = fxx; x < txx; x++) result[dir](x, y) += diff;
						x = txx - 1;
#else
						if (present_pixel != previous_pixel) {
							result[dir](x, y) +=
							 pixel_diff(previous_pixel, present_pixel);
						}
						weight[dir](x, y)++;
#endif
					}
				}
			}
		}
	}
	
	double score() {
		profile("Scoring");
		error result_tmp[number_of_images], result = 0;
		int weight_tmp[number_of_images], weight = 0;
#ifdef _OPENMP
#pragma omp parallel for if (number_of_images > 1)
#endif
		for (int id = 1; id < number_of_images; id++) {
			assert(images[id].width == width * DETAIL);
			assert(images[id].height == height * DETAIL);
			result_tmp[id] = 0; weight_tmp[id] = 0;
			for (int y = 0; y < height; y++)
			 for (int x = 0; x < width; x++) {
				int present_pixel = images[0](x, y);
				if (present_pixel == ignore_color) continue;
				int previous_pixel = images[id](
				 x * DETAIL + DETAIL / 2, y * DETAIL + DETAIL / 2);
				if (previous_pixel == ignore_color) continue;
				if (present_pixel != previous_pixel) {
					result_tmp[id] +=
					 pixel_diff(previous_pixel, present_pixel);
				}
				weight_tmp[id]++;
			}
		}
		for (int id = 1; id < number_of_images; id++) {
			result += result_tmp[id];
			weight += weight_tmp[id];
		}
		if (!quiet_mode) printf("Score %lf (%lld / %lld)\n",
		 (double)result / weight, (long long)result, (long long)weight);
		if (weight == 0) return result;
		return (double)result / weight;
	}
	
	void reflow() {
		profile("Reflow");
#ifdef _OPENMP
// Speed down!
//#pragma omp parallel for
#endif
		for (int id = 0; id < number_of_images; id++) {
			image<element> &images_tmp = images[id];
			image<element> &source_images_tmp = source_images[id];
#ifdef OPTIMIZE
			image<int> &image_change_tmp = image_change[id];
#endif
			if (id == 0) {
				images_tmp.copy(source_images_tmp);
#ifdef OPTIMIZE
				image_change_tmp.resize(width, height);
				for (int y = height - 1; 0 <= y; y--) {
					element last_color = ignore_color;
					int last_x = width;
					for (int x = width - 1; 0 <= x; x--) {
						element color = images_tmp(x, y);
						if (last_color != color) {
							last_x = x + 1;
							last_color = color;
						}
						image_change_tmp(x, y) = last_x;
					}
				}
#endif
				continue;
			}
			images_tmp.resize(width * DETAIL, height * DETAIL);
#ifdef OPTIMIZE
			image_change_tmp.resize(width * DETAIL, height * DETAIL);
#endif
			double ratio1 = id;
#ifndef LINEAR
			double ratio2 = id * id;
#endif
			for (int y = 0; y < height * DETAIL; y++) {
				element last_color = ignore_color;
				int last_x = width * DETAIL;
				for (int x = width * DETAIL - 1; 0 <= x; x--) {
					int xx = x / DETAIL, yy = y / DETAIL;
#ifdef LINEAR
					coordinate &f1 = flow[1](xx, yy);
					assert(!isnan(f1.x) && !isnan(f1.y));
					xx = (int)((x + 0.4999) / DETAIL - ratio1 * f1.x);
					yy = (int)((y + 0.4999) / DETAIL - ratio1 * f1.y);
#else
					coordinate &f1 = flow[1](xx, yy), &f2 = flow[2](xx, yy);
					assert(!isnan(f1.x) && !isnan(f1.y));
					assert(!isnan(f2.x) && !isnan(f2.y));
					xx = (int)((x + 0.4999) / DETAIL
					 - ratio1 * f1.x - ratio2 * f2.x);
					yy = (int)((y + 0.4999) / DETAIL
					 - ratio1 * f1.y - ratio2 * f2.y);
#endif
					element color;
					if (xx < 0 || width <= xx || yy < 0 || height <= yy) {
						color = ignore_color;
					} else {
						color = source_images_tmp(xx, yy);
					}
					images_tmp(x, y) = color;
					if (last_color != color) {
						last_x = x + 1;
						last_color = color;
					}
#ifdef OPTIMIZE
					image_change_tmp(x, y) = last_x;
#endif
				}
			}
		}
	}
	
#ifdef OPTIMIZE
	template<class T> inline T symmetry(image<T> &sum, int x, int y) {
		assert(0 <= y && y < sum.height);
		if (x == 0) return 0.0;
		if (x < 0) {
			assert(-x < sum.width);
			return sum(0, y) - sum(-x, y);
		}
		assert(x - 1 < sum.width);
		return sum(x - 1, y);
	}
#endif
	
	template<int dimension> void blockmatching_step(int radius, double step) {
		profile("Block matching step");
		image<error> error_map[DIRECTION]; image<int> weight_map[DIRECTION];
		int mod_x = width / 2 % radius, mod_y = height / 2 % radius;
#ifdef GAUSIANM
		int matching_radius = (int)(blockmatching_ratio * radius * 3);
#else
		int matching_radius = (int)(blockmatching_ratio * radius);
#endif
		double matching_sqradius = matching_radius * matching_radius;
		double weight_threshold = radius * radius * 0.05;
		profile_block("Map initialization")
		for (int id = 0; id < DIRECTION; id++) {
			error_map[id].resize(width, height, 1);
			weight_map[id].resize(width, height, 1);
		}
		evaluation<dimension>(error_map, weight_map, step);
		// Prepare weight values
		image<double> power_weight(
		 min(width + 1, matching_radius + 1),
		 min(height + 1, matching_radius + 1));
		vector<int> matching_range(matching_radius + 1);
		for (int y = 0; y <= matching_radius; y++) {
			int tx = (int)sqrt(matching_sqradius - y * y + 1e-5);
			matching_range[y] = min(matching_radius, tx);
		}
		profile_block("Power weight preparation")
#ifdef _OPENMP
#pragma omp parallel for
#endif
		for (int y = 0; y < power_weight.height; y++) {
			int tx = min(power_weight.width - 1, matching_range[y]);
			for (int x = 0; x <= tx; x++) {
				double norm = (double)x * x + (double)y * y;
#ifdef GAUSIANM
				power_weight(x, y) =
				 gausian_sqrt_fast(norm / matching_sqradius);
#else
				power_weight(x, y) =
				 hanning_sqrt_fast(norm / matching_sqradius);
#endif
			}
			for (int x = tx + 1; x < power_weight.width; x++) {
#ifndef NDEBUG
				double norm = (double)x * x + (double)y * y;
#ifdef GAUSIANM
				assert(gausian_sqrt_fast(norm / matching_sqradius) < 1e-6);
#else
				assert(hanning_sqrt_fast(norm / matching_sqradius) < 1e-6);
#endif
#endif
				power_weight(x, y) = 0.0;
			}
		}
#ifdef OPTIMIZE
		image<double> power_sum(power_weight.width, power_weight.height);
		profile_block("Addition of power")
#ifdef _OPENMP
#pragma omp parallel for
#endif
		for (int y = 0; y < power_sum.height; y++) {
			int tx = min(power_sum.width - 1, matching_range[y]);
			power_sum(0, y) = power_weight(0, y);
			for (int x = 1; x <= tx; x++) {
				power_sum(x, y) = power_sum(x - 1, y) + power_weight(x, y);
			}
			double power_sum_tmp = power_sum(tx, y);
			for (int x = tx + 1; x < power_sum.width; x++) {
				assert(power_weight(x, y) <= 1e-6);
				power_sum(x, y) = power_sum_tmp;
			}
#ifndef NDEBUG
			for (int x = 1 - power_sum.width; x < power_sum.width - 1; x++) {
				double power_tmp = symmetry(power_sum, x + 1, y)
				 - symmetry(power_sum, x, y);
				assert(fabs(power_tmp - power_weight(abs(x), y)) < 1e-6);
			}
#endif
		}
#endif
		// To store result values
		flow_summary[dimension].resize(width / radius + 1, height / radius + 1);
		image<double> error_summary[DIRECTION];
#ifdef OPTIMIZE
#ifdef _OPENMP
		image<int> error_nexts[DIRECTION];
		image<int> weight_nexts[DIRECTION];
#endif
#endif
		// Convolution error data and weight
		profile_block("Block matching main loop")
#ifdef _OPENMP
#pragma omp parallel for
#endif
		for (int dir = 0; dir < DIRECTION; dir++) {
			image<error> &error_map_tmp = error_map[dir];
			image<int> &weight_map_tmp = weight_map[dir];
#ifdef OPTIMIZE
#ifdef _OPENMP
			image<int> &error_next = error_nexts[dir];
			image<int> &weight_next = weight_nexts[dir];
#else
			image<int> error_next;
			image<int> weight_next;
#endif
			error_next.resize(width + 1, height);
			weight_next.resize(width + 1, height);
			profile_block("Addition of error and weight")
			for (int y = 0; y < height; y++) {
				int error_x = width, weight_x = width; int weight_type = -1;
				for (int x = width - 1; 0 <= x; x--) {
					// Set next position where type of weight changes
					int next_type; int &w = weight_map_tmp(x, y);
					if (w == number_of_images - 1) next_type = 1;
					else if (w == 0) next_type = 0;
					else next_type = 2;
					if (weight_type != next_type) {
						weight_x = x + 1; weight_type = next_type;
					}
					weight_next(x, y) = weight_x;
					// Set next position where error occurs
					if (error_map_tmp(x, y)) error_x = x;
					error_next(x, y) = error_x;
				}
#ifndef NDEBUG
				for (int x = 0; x < width; x++) {
					if (error_next(x, y) == width) continue;
					assert(error_map_tmp(error_next(x, y), y));
					if (x) {
						assert(error_next(x - 1, y) <= error_next(x, y));
						if (error_map_tmp(x, y))
						 assert(error_next(x, y) == x);
					}
				}
#endif
			}
#endif
#ifdef _OPENMP
		}
		int mode_dir_max[] = {1, DIRECTION};
		for (int mode_dir = 0; mode_dir < 2; mode_dir++)
#ifdef _OPENMP
#pragma omp parallel for if (mode_dir == 1)
#endif
		for (int dir = mode_dir; dir < mode_dir_max[mode_dir]; dir++) {
#ifdef OPTIMIZE
			image<int> &error_next = error_nexts[dir];
			image<int> &weight_next = weight_nexts[dir];
#endif
			image<error> &error_map_tmp = error_map[dir];
			image<int> &weight_map_tmp = weight_map[dir];
#endif
			image<double> &error_summary_tmp = error_summary[dir];
			error_summary_tmp.resize(width / radius + 1, height / radius + 1);
			for (int y = mod_y; y < height; y += radius)
			  for (int x = mod_x; x < width; x += radius) {
				int fx, tx, fy, ty;
				fx = max(0, x - matching_radius),
				tx = min(width - 1, x + matching_radius),
				fy = max(0, y - matching_radius),
				ty = min(height - 1, y + matching_radius);
				int rx = x / radius, ry = y / radius;
#ifdef OPTIMIZE
				// If the area is useless in the primary layer
				if (dir) {
					double e = error_summary[0](rx, ry);
					// error_summary_tmp(rx, ry) = 1e99;
					if (e == 0.0 || 1e98 < e) continue;
				}
				// No features in the window
				int feature = 0;
				for (int yy = fy; yy <= ty; yy++) {
					int dyy = abs(yy - y);
					fx = max(0, x - matching_range[dyy]);
					tx = min(width - 1, x + matching_range[dyy]);
					if (error_next(fx, yy) <= tx) {
						assert(error_map_tmp(error_next(fx, yy), yy));
						feature = 1;
						break;
					}
				}
				if (!feature) {
					error_summary_tmp(rx, ry) = 0.0;
					continue;
				}
#endif
				double error_value_tmp = 0.0, weight_value_tmp = 0.0;
				profile_block("Convolution")
				for (int yy = fy; yy <= ty; yy++) {
					int dyy = abs(yy - y);
					profile_count(0);
					fx = max(0, x - matching_range[dyy]);
					tx = min(width - 1, x + matching_range[dyy]);
#ifdef OPTIMIZE
					// If the line is full weighted
					if (tx < weight_next(fx, yy) &&
					 weight_map_tmp(fx, yy) == number_of_images - 1) {
						double power_sum_tmp =
						 symmetry(power_sum, tx - x + 1, dyy)
						 - symmetry(power_sum, fx - x, dyy);
						weight_value_tmp +=
						 power_sum_tmp * (number_of_images - 1);
#ifndef NDEBUG
						double power_sum_test = 0.0;
						for (int xx = fx; xx <= tx; xx++) {
							double weight = power_weight(abs(xx - x), dyy);
							assert(weight_map_tmp(xx, yy)
							 == number_of_images - 1);
							assert(fabs(symmetry(power_sum, xx - x + 1, dyy)
							 - symmetry(power_sum, xx - x, dyy)
							 - weight) < 1e-6);
							power_sum_test += weight;
						}
						if (1e-6 < fabs(power_sum_test - power_sum_tmp))
						 assert(fabs(power_sum_test - power_sum_tmp) <= 1e-6);
#endif
						// Calculate error value
						for (int xx = fx; xx <= tx;) {
							int nxt = error_next(xx, yy);
							if (nxt != xx) {
								xx = nxt;
								continue;
							}
							error_value_tmp +=
							 error_map_tmp(xx, yy) *
							 power_weight(abs(xx - x), dyy);
							xx++;
						}
						continue;
					}
					profile_count(3);
#endif
#ifdef OPTIMIZE
					for (int xx = fx; xx <= tx;) {
						int w = weight_map_tmp(xx, yy);
						profile_count(4);
						if (w == 0) {
							xx = weight_next(xx, yy);
							continue;
						}
						int ttx = min(tx, weight_next(xx, yy) - 1);
						if (w != number_of_images - 1) {
							for (; xx <= ttx; xx++) {
								double weight = power_weight(abs(xx - x), dyy);
								error_value_tmp +=
								 error_map_tmp(xx, yy) * weight;
								weight_value_tmp +=
								 weight_map_tmp(xx, yy) * weight;
							}
							continue;
						}
						double power_sum_tmp =
						 symmetry(power_sum, ttx - x + 1, dyy)
						 - symmetry(power_sum, xx - x, dyy);
						weight_value_tmp +=
						 power_sum_tmp * (number_of_images - 1);
						for (; xx <= ttx;) {
							int nxt = error_next(xx, yy);
							if (nxt != xx) {
								xx = nxt;
								continue;
							}
							double weight = power_weight(abs(xx - x), dyy);
							error_value_tmp +=
							 error_map_tmp(xx, yy) * weight;
							xx++;
						}
						xx = ttx + 1;
					}
#else
					for (int xx = fx; xx <= tx; xx++) {
						double weight = power_weight(abs(xx - x), dyy);
						error_value_tmp += error_map_tmp(xx, yy) * weight;
						weight_value_tmp += weight_map_tmp(xx, yy) * weight;
					}
#endif
				}
				if (dir == 0) {
					if (weight_value_tmp <= weight_threshold) {
						error_summary_tmp(rx, ry) = 1e99;
						continue;
					}
				} else {
					if (weight_value_tmp < weight_threshold) {
						error_summary_tmp(rx, ry) = 1e99;
						continue;
					}
				}
				assert(-1e-9 < error_value_tmp);
				error_summary_tmp(rx, ry) = error_value_tmp / weight_value_tmp;
				assert(error_summary_tmp(rx, ry) < 1e20);
			}
		}
		double dx[] = {0, -step, step, 0, 0, step, step, -step, -step};
		double dy[] = {0, 0, 0, -step, step, step, -step, step, -step};
		for (int y = mod_y; y < height; y += radius)
		  for (int x = mod_x; x < width; x += radius) {
			int rx = x / radius, ry = y / radius;
			flow_summary[dimension](rx, ry) = flow[dimension](x, y);
			double e = error_summary[0](rx, ry);
			if (e == 0.0 || 1e98 < e) continue;
			double min_error = e; int min_dir = 0;
			for (int i = 1; i < DIRECTION; i++) {
				double target_error = error_summary[i](rx, ry);
				if (target_error < min_error * 0.99999) {
					min_error = target_error;
					min_dir = i;
				}
			}
			assert(-1e-9 < min_error);
			flow_summary[dimension](rx, ry) +=
			 coordinate(dx[min_dir], dy[min_dir]);
		}
		deploy_flow_summary<dimension>(radius);
	}
	
	template<int dimension> void deploy_flow_summary(int radius) {
		profile("Deploy flow summary");
		int mod_x = width / 2 % radius, mod_y = height / 2 % radius;
		int matching_radius = (int)(blockmatching_ratio * radius);
#ifdef BILINEAR
		int matching_radius2 = matching_radius;
#elif defined GAUSIAN
		int matching_radius2 = (int)(blockmatching_ratio * radius * 3);
#else
		int matching_radius2 = (int)(blockmatching_ratio * radius * 2);
#endif
		double matching_sqradius = matching_radius * matching_radius;
		vector<int> matching_range(max(radius + 1, matching_radius2 + 1));
		for (int y = 0; y <= matching_radius2; y++) {
			int tx = (int)sqrt(
			 matching_radius2 * matching_radius2 - y * y + 1e-5);
			matching_range[y] = min(matching_radius2, tx);
		}
		image<double> interpolate_weight(
		 min(width, max(radius + 1, matching_radius2 + 1)),
		 min(height, max(radius + 1, matching_radius2 + 1)));
		profile_block("Interpolate weight generation")
		for (int y = 0; y < interpolate_weight.height; y++) {
			int tx = min(interpolate_weight.width - 1, matching_range[y]);
			for (int x = 0; x <= tx; x++) {
				double norm = (double)x * x + (double)y * y;
#ifdef BILINEAR
				interpolate_weight(x, y) =
				 bilinear_sqrt_fast(norm / matching_sqradius);
#elif defined GAUSIAN
				interpolate_weight(x, y) =
				 gausian_sqrt_fast(norm / matching_sqradius);
#else
				interpolate_weight(x, y) =
				 bicubic_sqrt_fast(norm / matching_sqradius);
#endif
			}
			for (int x = tx + 1; x < interpolate_weight.width; x++)
			 interpolate_weight(x, y) = 0.0;
		}
		/*
		image<coordinate> flow_tmp(width, height, 1);
		image<double> flow_weight(width, height, 1);
#ifdef _OPENMP
#pragma omp parallel for
		for (int thread = 0; thread < THREAD_NUM; thread++)
#endif
		for (int y = mod_y; y < height; y += radius)
		  for (int x = mod_x; x < width; x += radius) {
			// if (!flow_diff_set(x / radius, y / radius)) continue;
			coordinate f = flow_summary[dimension](x / radius, y / radius);
			int fx, tx, fy, ty;
			fx = max(0, x - matching_radius2);
			tx = min(width - 1, x + matching_radius2);
			fy = max(0, y - matching_radius2);
			ty = min(height - 1, y + matching_radius2);
#ifdef _OPENMP
			fy = fy + ((-fy + thread) % THREAD_NUM + THREAD_NUM) % THREAD_NUM;
#endif
			for (int yy = fy; yy <= ty; yy += THREAD_NUM) {
				fx = max(0, x - matching_range[abs(yy - y)]);
				tx = min(width - 1, x + matching_range[abs(yy - y)]);
				for (int xx = fx; xx <= tx; xx++) {
					double weight =
					 interpolate_weight(abs(xx - x), abs(yy - y));
					assert(-1.0 <= weight && weight < 1.0 + 1e-9);
					coordinate &flow_tmp_link = flow_tmp(xx, yy);
					coordinate value = f * weight;
					double &flow_weight_link = flow_weight(xx, yy);
					flow_tmp_link += value;
					flow_weight_link += weight;
				}
			}
		}
#ifdef _OPENMP
#pragma omp parallel for
#endif
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				if (1e-9 < flow_weight(x, y)) {
					flow[dimension](x, y) = flow_tmp(x, y) / flow_weight(x, y);
				}
			}
		}
		*/
		image<coordinate> &flow_tmp = flow[dimension];
		image<coordinate> &flow_summary_tmp = flow_summary[dimension];
		assert(width <= flow_tmp.width && height <= flow_tmp.height);
#ifdef _OPENMP
#pragma omp parallel for
#endif
		profile_block("Deploy flow main loop")
		for (int y = 0; y < height; y++){
			for (int x = 0; x < width; x++) {
				coordinate f; double w = 0.0;
				int fx, tx, fy, ty;
				fy = max(0, y - matching_radius);
				fy = (fy - mod_y + radius - 1) / radius;
				ty = min(height - 1, y + matching_radius);
				ty = (ty - mod_y) / radius;
				for (int yy = fy; yy <= ty; yy++) {
					int dyy = abs(yy * radius + mod_y - y);
					fx = max(0, x - matching_range[dyy]);
					fx = (fx - mod_x + radius - 1) / radius;
					tx = min(width - 1, x + matching_range[dyy]);
					tx = (tx - mod_x) / radius;
					for (int xx = fx; xx <= tx; xx++) {
						double weight =
						 interpolate_weight(abs(xx * radius + mod_x - x), dyy);
						assert(!isnan(flow_summary_tmp(xx, yy).x));
						assert(!isnan(flow_summary_tmp(xx, yy).y));
						f += flow_summary_tmp(xx, yy) * weight;
						w += weight;
					}
				}
				if (w < 0.01) {
					// Adopt the flow of the nearest point for undefined pixels
					flow_tmp(x, y) = flow_summary_tmp(
					 max(0, min(flow_summary_tmp.width - 1,
					 (x - mod_x + radius / 2) / radius)),
					 max(0, min(flow_summary_tmp.height - 1,
					 (y - mod_y + radius / 2) / radius)));
				} else {
					assert(!isnan(f.x) && !isnan(f.y) && !isnan(w));
					flow_tmp(x, y) = f / w;
				}
			}
		}
		reflow();
	}
	
	void blockmatching(int radius, double step = 1.0, double limit = 0.5) {
		profile("Block matching");
		double last_score = score();
		while (limit <= step) {
			if (!quiet_mode) printf("Block Matching: %d %lf\n", radius, step);
			blockmatching_step<1>(radius, step);
#ifndef LINEAR
			if (2 < number_of_images) {
				blockmatching_step<2>(radius,
				 step / (number_of_images - 1) * 0.5);
			}
#endif
			if (verbose_mode) flow[1].debug();
			double present_score = score();
			if (last_score * 0.99 <= present_score) step *= 0.5;
			last_score = present_score;
		}
	}
	
	coordinate predict(double x, double y, double ratio) {
		profile("Predict");
		coordinate history_sum; int history_count = 0;
		coordinate p, p_sum;
		for (int t = 0; t < 5; t++) {
			coordinate f = coordinate(x, y) - p;
			int xx = (int)round(f.x), yy = (int)round(f.y);
			xx = min(width - 1, max(0, xx));
			yy = min(height - 1, max(0, yy));
			f += flow[1](xx, yy) * ratio - coordinate(x, y);
			p += f * pow(0.8, t);
		}
		for (int t = 0; t < 5; t++) {
			coordinate f = coordinate(x, y) - p;
			int xx = (int)round(f.x), yy = (int)round(f.y);
			xx = min(width - 1, max(0, xx));
			yy = min(height - 1, max(0, yy));
			f += flow[1](xx, yy) * ratio - coordinate(x, y);
			p += f * 0.3;
			coordinate q = coordinate(x, y) - p;
			history_sum += p;
			history_count++;
		}
		return coordinate(x, y) - history_sum / history_count;
	}
	
	void adaptive_blockmatching() {
		profile("Adaptive matching");
		int size;
		blockmatching(max(width, height));
		for (size = min(min(width, height) / 2 - 1,
		 (int)round(max(width, height) * alternation_ratio));
		 size > block_size; size = (int)(size * alternation_ratio)) {
			blockmatching(size, 2, 0.5 / (number_of_images - 1));
		}
		/*
		for (int ttt = 0; ttt < 10; ttt++) {
			blockmatching_step<1>(1, 1.0);
			blockmatching_step<1>(2, 1.0);
			blockmatching_step<1>(4, 1.0);
			blockmatching_step<1>(8, 1.0);
			blockmatching_step<1>(16, 1.0);
		}
		blockmatching_step<1>(1, 1.0);
		for (size = 30;
		 size > block_size; size = (int)(size * alternation_ratio)) {
			blockmatching(size, 2, 0.5 / (number_of_images - 1));
		}
		*/
		// blockmatching(1, 2, 0.5 / (number_of_images - 1));
		blockmatching(block_size, 1, 1.0 / (number_of_images - 1));
	}
	
	optical_flow(image<element> *input, int num):
	 width(input[0].width), height(input[0].height),
	 number_of_images(num), source_images(input) {
#ifdef _OPENMP
		hanning_sqrt_fast(0.0);
#ifdef BILINEAR
		bilinear_sqrt_fast(0.0);
#elif defined GAUSIAN
		gausian_sqrt_fast(0.0);
#else
		bicubic_sqrt_fast(0.0);
#endif
#endif
		profile("Optical flow constructer");
		images = new image<element>[num];
#ifdef OPTIMIZE
		image_change = new image<element>[num];
#endif
		flow[1].resize(width, height);
#ifndef LINEAR
		flow[2].resize(width, height);
#endif
		reflow();
		if (verbose_mode) fprintf(stderr, "Optical flow is initialized\n");
		if (1 < num) score();
	}
	
	~optical_flow() {
		delete[] images;
#ifdef OPTIMIZE
		delete[] image_change;
#endif
	}
};

////////////////////////////////////////////////////////////////////////////////
// OPTION ANALYZER
////////////////////////////////////////////////////////////////////////////////
// Analyze color value
pixel option_color(string arg) {
	// Read value as a number
	char *endptr;
	int result = strtol(arg.c_str(), &endptr, 0);
	if (strlen(endptr) == 0) return result;
	// Look up value from color names
	for (int i = 0; i < (int)arg.size(); i++)
	 arg[i] = tolower(arg[i]);
	if (arg == "black") return 0x000000;
	if (arg == "white") return 0xffffff;
	if (arg == "red") return 0xff0000;
	if (arg == "green") return 0x00ff00;
	if (arg == "blue") return 0x0000ff;
	if (arg == "undefined") return -1;
	if (arg == "none") return -1;
	throw "invalid color";
}

// Analyze to command line options
vector<string> option_analyzer(int argc, char **argv) {
	vector<string> result;
	
	// Set default values
	prediction_mode = false;
	verbose_mode = false;
	quiet_mode = false;
	output_file = "";
	block_size = 10;
	delay_ratio = 1.0;
	blockmatching_ratio = 3.0;
	flow_file = "";
	ignore_color = -1;
	undefined_color = -1;
	alternation_ratio = 0.9;
	convert_command = "convert";
	// Analyze options
	for (int i = 1; i < argc; i++) {
		string key = "", val = ""; int skip = 0;
		if (argv[i][0] == '-' && argv[i][1] != 0) {
			// Long option
			if (argv[i][1] == '-') {
				// Split by "=" character
				key = strtok(argv[i] + 2, "=");
				char *cval = strtok(NULL, "");
				if (cval == NULL) val = ""; else val = cval;
			// Short option
			} else {
				// Short option must be just one letter
				if (strlen(argv[i]) != 2)
				 throw string("invalid option ") + string(argv[i]);
				// Set key
				key = argv[i] + 1;
				// Option value
				if (i + 1 < argc) val = argv[i + 1];
				skip = 1;
			}
			// Prediction mode
			if (key == "prediction" || key == "p") {
				prediction_mode = true;
			// Verbose mode
			} else if (key == "verbose" || key == "v") {
				verbose_mode = true;
				quiet_mode = false;
			// Quiet mode
			} else if (key == "quiet" || key == "q") {
				quiet_mode = true;
				verbose_mode = false;
			// Output file name
			} else if (key == "output" || key == "o") {
				output_file = val;
				i += skip;
			// Block size
			} else if (key == "block" || key == "b") {
				block_size = atoi(val.c_str());
				i += skip;
			// Delay ratio to predict
			} else if (key == "delay" || key == "d") {
				delay_ratio = atof(val.c_str());
				i += skip;
			// Blockmatching scale
			} else if (key == "scale" || key == "s") {
				blockmatching_ratio = atof(val.c_str());
				i += skip;
			// Input flow file
			} else if (key == "flow" || key == "f") {
				flow_file = val;
				i += skip;
			// Location X
			} else if (key == "x") {
				target_x = atof(val.c_str());
				i += skip;
			// Location Y
			} else if (key == "y") {
				target_y = atof(val.c_str());
				i += skip;
			// Location Z
			} else if (key == "z") {
				target_z = atof(val.c_str());
				i += skip;
			// Color to ignore
			} else if (key == "ignore" || key == "i") {
				try_string {
					ignore_color = option_color(val);
				} catch_string(msg) {
					throw string("option ") + key + string(": ") + msg;
				}
				i += skip;
			// Color for pixels which are undefined
			} else if (key == "undefined" || key == "u") {
				try_string {
					undefined_color = option_color(val);
				} catch_string(msg) {
					throw string("option ") + key + string(": ") + msg;
				}
				i += skip;
			// Alternation ratio
			} else if (key == "alternation" || key == "a") {
				alternation_ratio = atof(val.c_str());
				if (alternation_ratio < 0.00001)
				 throw "alternation ratio must be at least 0.00001";
				if (0.9999 < alternation_ratio)
				 throw "alternation ratio must be at most 0.9999";
				i += skip;
			// Path of convert command
			} else if (key == "convert" || key == "c") {
				convert_command = val;
				i += skip;
			// Help mode
			} else if (key == "help" || key == "h") {
				throw "";
			// If an undefined option is given
			} else {
				throw string("unrecognized option ") + string(argv[i]);
			}
		} else {
			// Add a file name if it is non-option value
			result.push_back(argv[i]);
		}
	}
	return result;
}

////////////////////////////////////////////////////////////////////////////////
// MAIN FUNCTIONS
////////////////////////////////////////////////////////////////////////////////
void main_function() {
	picture images[file_names.size()];
	for (int i = 0; i < (int)file_names.size(); i++)
	 images[i].load(file_names[i]);
	optical_flow<pixel> of(images, file_names.size());
	if (flow_file != "") {
		of.flow_summary[1].load(flow_file);
		of.deploy_flow_summary<1>(block_size);
	}
	if (1 < file_names.size()) of.adaptive_blockmatching();
	if (1e-4 < hypot(target_x, target_y)) {
		for (int i = -40; i < 360; i++) {
			map<pixel, int> candidates; candidates.clear();
			coordinate sum(0.0, 0.0);
			for (int r = 0; r < 10; r++) {
				double base_r = i * delay_ratio * r * target_z;
				for (int a = 0; a < 36; a++) {
					double tx = target_x + base_r * cos(a / 4.5 * atan(1)),
					 ty = target_y + base_r * sin(a / 4.5 * atan(1));
					coordinate f = of.predict(tx, ty, i * delay_ratio);
					sum += f;
					int xx = (int)round(f.x), yy = (int)round(f.y);
					if (xx < 0 || of.width <= xx ||
					 yy < 0 || of.height <= yy) {
						if (undefined_color != -1) {
							candidates[undefined_color]++;
							continue;
						} else {
							xx = min(of.width - 1, max(0, xx));
							yy = min(of.height - 1, max(0, yy));
						}
					}
					candidates[of.source_images[0](xx, yy)]++;
				}
			}
			sum /= 360; sum -= coordinate(target_x, target_y);
			printf("%d %.3lf %.3lf %d", i, sum.x, sum.y, (int)candidates.size());
			for (map<pixel, int>::iterator it = candidates.begin();
			 it != candidates.end(); it++) {
				printf(" %s %d", cstr(it->first).c_str(), it->second);
			}
			puts("");
		}
	} else if (prediction_mode) {
		if (output_file == "") throw "output file name is required";
		picture future(of.width, of.height);
		for (int y = 0; y < of.height; y++)
		 for (int x = 0; x < of.width; x++) {
			coordinate f = of.predict(x, y, delay_ratio);
			int xx = (int)round(f.x), yy = (int)round(f.y);
			if (xx < 0 || of.width <= xx ||
			 yy < 0 || of.height <= yy) {
				if (undefined_color != -1) {
					future(x, y) = undefined_color;
					continue;
				} else {
					xx = min(of.width - 1, max(0, xx));
					yy = min(of.height - 1, max(0, yy));
				}
			}
			future(x, y) = of.source_images[0](xx, yy);
		}
		future.save(output_file);
	} else {
		if (output_file != "")
		 of.flow_summary[1].save(output_file);
	}
}

int main(int argc, char **argv) {
#ifdef _OPENMP
	omp_set_num_threads(THREAD_NUM);
#endif
	try_string {
		if (argc == 1) throw "";
		file_names = option_analyzer(argc, argv);
		if (verbose_mode) {
			fprintf(stderr, "Prediction mode:        %s\n",
			 prediction_mode ? "Yes" : "No");
			fprintf(stderr, "Verbose mode:           %s\n",
			 verbose_mode ? "Yes" : "No");
			fprintf(stderr, "Output file:            %s\n",
			 output_file.c_str());
			fprintf(stderr, "Block size:             %d\n",
			 block_size);
			fprintf(stderr, "Delay ratio:            %lf\n",
			 delay_ratio);
			fprintf(stderr, "Blockmatching scale:    %lf\n",
			 blockmatching_ratio);
			fprintf(stderr, "Flow file:              %s\n",
			 flow_file.c_str());
			if (1e-4 < hypot(target_x, target_y)) {
				fprintf(stderr, "Target Location:        (%lf,%lf),%lf\n",
				 target_x, target_y, target_z);
			}
			fprintf(stderr, "Ignore color:           ");
			if (ignore_color < 0) fprintf(stderr, "None\n");
			else fprintf(stderr, "%06X\n", ignore_color);
			fprintf(stderr, "Undefined color:        ");
			if (undefined_color < 0) fprintf(stderr, "None\n");
			else fprintf(stderr, "%06X\n", undefined_color);
			fprintf(stderr, "Alternation ratio:      %lf\n",
			 alternation_ratio);
			fprintf(stderr, "Convert command:        %s\n",
			 convert_command.c_str());
			fprintf(stderr, "Number of arguments:    %d\n",
			 (int)file_names.size());
			fprintf(stderr, "List of file names:    ");
			for (int i = 0; i < (int)file_names.size(); i++) {
				fprintf(stderr, " %d:\"%s\"", (int)i, file_names[i].c_str());
			}
			fprintf(stderr, "\n");
			fprintf(stderr, "\n");
		}
		if (flow_file == "" && file_names.size() == 1) {
			throw "two or more images are required";
		}
	} catch_string(msg) {
		if (msg.size()) fprintf(stderr,
		 PROGRAM "-" VERSION ": %s\n", msg.c_str());
		else fprintf(stderr,
		 "Usage:\n"
		 "  " PROGRAM " [options] present_image [previous_image ...]\n"
		 "\nOptions:\n"
		 "  --prediction or -p:           Enable prediction mode.\n"
		 "  --verbose or -v:              Print detailed information.\n"
		 "  --quiet or -q:                Suppress all warning messages.\n"
		 "  --output=file or -o file:     Set output file name.\n"
		 "  --block=int or -b int:        Set minimum block size.\n"
		 "  --delay=float or -d float:    Set delay ratio to predict.\n"
		 "  --scale=float or -s float:    Set block matching scale.\n"
		 "  --flow=file or -f file:       Input from the flow file.\n"
		 "  --x=float or -x float:        Location to predict.\n"
		 "  --y=float or -y float:        Location to predict.\n"
		 "  --z=float or -z float:        Error scale to predict.\n"
		 "  --ignore=color or -i color:\n"
		 "                Ignore pixels of color as undefined\n"
		 "  --undefined=color or -u color:\n"
		 "                Paint color in undefined pixels.\n"
		 "  --alternation=float or -a float:\n"
		 "                Set alternation ratio between hierarchies.\n"
		 "  --convert=command or -c commnad:\n"
		 "                Use the specified command to convert image type.\n"
		 "\nVersion: " PROGRAM " " VERSION " " RELEASE_DATE "\n"
		 "\nCopyright(C) 2011 Imajo Kentaro. All rights reserved.\n\n");
		return 1;
	}
	try_string {
		try_string { exec(convert_command + " --version >/dev/null 2>&1"); }
		catch_string(msg) { throw "convert command is not found"; }
		main_function();
	} catch_string(msg) {
		printf(PROGRAM "-" VERSION ": %s\n", msg.c_str());
		return 1;
	}
	return 0;
}

