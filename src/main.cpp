#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS

#define NOMINMAX
#include <Windows.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "util/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "util/stb_image_write.h"

#include "maths/vec.h"

#include <vector>
#include <cmath>
#include <algorithm>

#include <thread>
#include <atomic>

#include <stdio.h>


using u8 = unsigned char;
using rgba8u = vec<4, u8>;

using vec4f = vec<4, float>;


constexpr int xres = 720;//480;//128 * 4;
constexpr int yres = 720;//480;//128 * 4;
constexpr int num_frames  = 30 * 12;
constexpr int noise_size  = 1 << 8;
constexpr int num_samples = 6;
constexpr double inv_num_samples = 1.0 / num_samples;


const bool save_gif = false;
const bool ramdrive = true;
const char * dir_prefix = (ramdrive ? "r:" : ".");



inline static vec3f hsv2rgb_smooth(const vec3f & c)
{
	const vec3f
		i = vec3f(0, 4, 2) + (c.x() * 6),
		j = vec3f(
			std::max(0.0f, std::min(1.0f, std::fabsf(std::fmod(i.x(), 6.0f) - 3.0f) - 1.0f)),
			std::max(0.0f, std::min(1.0f, std::fabsf(std::fmod(i.y(), 6.0f) - 3.0f) - 1.0f)),
			std::max(0.0f, std::min(1.0f, std::fabsf(std::fmod(i.z(), 6.0f) - 3.0f) - 1.0f))),
		k = c * c * (vec3f(3) - c * 2) - 1;

	return (k * c.y() + 1) * c.z();
}

inline double LinearMapping(double a, double b, double c, double d, double x) { return (x - a) / (b - a) * (d - c) + c; }


vec4f ImageFunction(double frame, double x, double y, int num_frames, int xres, int yres)
{
	constexpr int num_iters = 4096;

	const double time  = LinearMapping(0, num_frames, 0.0, 3.141592653589793238 * 2, frame);
	const double time2 = cos(3.141592653589793238 - time) * 0.5 + 0.5;
	const double scale = exp(time2 * -12.0);

	const vec2d centre = vec2d(-0.761574, -0.0847596);
	const vec2d z0 = vec2d(
		LinearMapping(0, xres, -scale, scale, x),
		LinearMapping(0, yres, scale, -scale, y)) + centre;

	vec2d z = z0;
	int iteration = 0;
	for (; iteration < num_iters && (dot(z, z) < 25 * 25); iteration++)
		z = vec2d(z.x() * z.x() - z.y() * z.y(), 2 * z.x() * z.y()) + z0;

	const int blah = iteration == num_iters ? 0 : (z.y() > 0);

	const vec4f cheshire[2] = { vec4f(160, 100, 200, 1) / 256, vec4f(137, 25, 100) / 256 };
	const vec4f col_out = cheshire[iteration % 2] * blah;
	return col_out * col_out * 3.0f;
}

inline double sign(double v) { return (v == 0) ? 0 : (v > 0) ? (double)1 : (double)-1; }

// Convert uniform distribution into triangle-shaped distribution, from https://www.shadertoy.com/view/4t2SDh
inline double triDist(double v)
{
	const double orig = v * 2 - 1;
	v = orig / sqrt(std::abs(orig));
	v = std::max((double)-1, v); // Nerf the NaN generated by 0*rsqrt(0). Thanks @FioraAeterna!
	v = v - sign(orig);
	return v;
}

template<int b>
constexpr inline double RadicalInverse(int i)
{
	constexpr double inv_b = 1.0 / b;
	double f = 1, r = 0;
	while (i > 0)
	{
		const int i_div_b = i / b;
		const int i_mod_b = i - b * i_div_b;
		f *= inv_b;
		r += i_mod_b * f;
		i  = i_div_b;
	}
	return r;
}

void hilbert(const vec2i dx, const vec2i dy, const int size_x, const int size_y, vec2i p, int size, std::vector<int> & ordering_out)
{
	if (size > 1)
	{
		size >>= 1;
		hilbert( dy,  dx, size_x, size_y, p, size, ordering_out); p += dy * size;
		hilbert( dx,  dy, size_x, size_y, p, size, ordering_out); p += dx * size;
		hilbert( dx,  dy, size_x, size_y, p, size, ordering_out); p += dx * (size - 1) - dy;
		hilbert(-dy, -dx, size_x, size_y, p, size, ordering_out);
	}
	else if (p.x() < size_x && p.y() < size_y)
		ordering_out.push_back(p.y() * size_x + p.x());
}

void RenderThreadFunc(
	const int f,
	const vec2d * const samples,
	const uint16_t * const noise,
	std::atomic<int> * const counter,
	rgba8u * const image_out)
{
	// Get rounded up number of buckets in x and y
	constexpr int bucket_size = 8;
	const int x_buckets = (xres + bucket_size - 1) / bucket_size;
	const int y_buckets = (yres + bucket_size - 1) / bucket_size;
	const int num_buckets = x_buckets * y_buckets;

	while (true)
	{
		// Get the next bucket index atomically and exit if we're done
		const int bucket = counter->fetch_add(1);
		if (bucket >= num_buckets)
			break;

		// Get sub-pass and pixel ranges for current bucket
		const int sub_pass  = bucket / num_buckets;
		const int bucket_p  = bucket - num_buckets * sub_pass;
		const int bucket_y  = bucket_p / x_buckets;
		const int bucket_x  = bucket_p - x_buckets * bucket_y;
		const int bucket_x0 = bucket_x * bucket_size, bucket_x1 = std::min(bucket_x0 + bucket_size, xres);
		const int bucket_y0 = bucket_y * bucket_size, bucket_y1 = std::min(bucket_y0 + bucket_size, yres);

		for (int y = bucket_y0; y < bucket_y1; ++y)
		for (int x = bucket_x0; x < bucket_x1; ++x)
		{
			vec4f sum = 0;
			for (int s = 0; s < num_samples; s++)
			{
				double i = s * inv_num_samples; i += noise[(y % noise_size) + (x % noise_size)] * (1.0 / 65536); i = (i < 1) ? i : i - 1;
				double j = samples[s].x();      j += noise[(y % noise_size) + (x % noise_size)] * (1.0 / 65536); j = (j < 1) ? j : j - 1;
				double k = samples[s].y();      k += noise[(y % noise_size) + (x % noise_size)] * (1.0 / 65536); k = (k < 1) ? k : k - 1;

				sum += ImageFunction(
					f + 0.5 + triDist(i),
					x + 0.5 + triDist(j),
					y + 0.5 + triDist(k),
					num_frames, xres, yres);
			}
			sum *= inv_num_samples;

			image_out[y * xres + x] =
			{
				std::min(255, (int)(std::sqrt(sum.x()) * 256)),
				std::min(255, (int)(std::sqrt(sum.y()) * 256)),
				std::min(255, (int)(std::sqrt(sum.z()) * 256)),
				255 //std::min(255, (int)(sum.w() * 256))
			};
		}
	}
}

int main(int argc, char ** argv)
{
#ifdef _WIN32
	SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#endif
#if _DEBUG
	const int num_threads = std::thread::hardware_concurrency();
#else
	const int num_threads = std::thread::hardware_concurrency();
#endif

	printf("Rendering %d frames at res %d x %d with %d samples per pixel\n", num_frames, xres, yres, num_samples);

	std::vector<rgba8u> image(xres * yres);

	std::vector<vec2d> samples(num_samples);
	for (int s = 0; s < num_samples; s++)
		samples[s] = { RadicalInverse<2>(s), RadicalInverse<3>(s) };

	std::vector<uint16_t> noise(noise_size * noise_size);
	{
		std::vector<int> hilbert_vals;
		hilbert_vals.reserve(noise_size * noise_size);
		hilbert(vec2i(1, 0), vec2i(0, 1), noise_size, noise_size, 0, noise_size, hilbert_vals);
	
		uint64_t v = 0;
		for (int i = 0; i < noise_size * noise_size; ++i)
		{
			v += 11400714819323198487ull;
			noise[hilbert_vals[i]] = (uint16_t)(v >> 48);
		}
	}

	std::vector<std::thread> render_threads(num_threads);

	for (int f = 0; f < num_frames; f++)
	{
		char filename[256];
		sprintf(filename, "%s/frames/frame%02d.png", dir_prefix, f);

		const auto t_start = std::chrono::system_clock::now();

		std::atomic<int> counter = 0;

		for (int z = 0; z < num_threads; ++z) render_threads[z] = std::thread(RenderThreadFunc, f, &samples[0], &noise[0], &counter, &image[0]);
		for (int z = 0; z < num_threads; ++z) render_threads[z].join();

		const auto t_end = std::chrono::system_clock::now();
		const std::chrono::duration<double> elapsed_time = t_end - t_start;
		const double elapsed_seconds = elapsed_time.count();
		//printf("took %.2f seconds\n", elapsed_seconds);

		stbi_write_png(filename, xres, yres, 4, &image[0], xres * 4);

		printf("%d ", f);
	}
	printf("\n");

	return 0;
}
