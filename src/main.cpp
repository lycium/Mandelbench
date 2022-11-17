#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS

#define NOMINMAX
#include <Windows.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "util/stb_image_write.h"

#include "maths/vec.h"

#include <algorithm>
#include <vector>
#include <cmath>
#include <thread>
#include <atomic>

#include <stdio.h>


using u8 = unsigned char;
using rgba8u = vec<4, u8>;
using vec4f  = vec<4, float>;

constexpr int xres = 720;
constexpr int yres = 720;
constexpr int num_frames  = 30 * 12;
constexpr int noise_size  = 1 << 8;
constexpr int num_samples = 6 * 6 * 6;
constexpr double inv_num_samples = 1.0 / num_samples;

const bool save_frames = true;
const bool ramdrive = false;
const char * dir_prefix = (ramdrive ? "r:" : ".");



inline double LinearMapping(double a, double b, double c, double d, double x) { return (x - a) / (b - a) * (d - c) + c; }

inline vec4f ImageFunction(double frame, double x, double y, int num_frames, int xres, int yres)
{
	constexpr int num_iters = 4096;

	const double time  = LinearMapping(0, num_frames, 0.0, 3.141592653589793238 * 2, frame);
	const double time2 = std::cos(3.141592653589793238 - time) * 0.5 + 0.5;
	const double scale = std::exp(time2 * -12.0);
	const vec2d centre = vec2d(-0.761574, -0.0847596);
	const vec2d z0 = vec2d(
		LinearMapping(0, xres, -scale, scale, x),
		LinearMapping(0, yres, scale, -scale, y)) + centre;

	// Fast early out for main cardioid and period 2 bulb
	// Ref: https://en.wikipedia.org/wiki/Plotting_algorithms_for_the_Mandelbrot_set#Cardioid_/_bulb_checking
	const double q = (z0.x() - 0.25) * (z0.x() - 0.25) + z0.y() * z0.y();
	const bool cardioid = (q * (q + (z0.x() - 0.25)) <= 0.25 * z0.y() * z0.y());
	const bool bulb2 = ((z0.x() + 1) * (z0.x() + 1) + z0.y() * z0.y() < 0.0625);

	if (cardioid || bulb2)
		return vec4f(0);
	else
	{
		vec2d z = z0;
		int iteration = 0;
		for (; iteration < num_iters && (dot(z, z) < 25 * 25); iteration++)
			z = vec2d(z.x() * z.x() - z.y() * z.y(), 2 * z.x() * z.y()) + z0;

		// Binary decomposition colouring, see https://mathr.co.uk/mandelbrot/book-draft/#binary-decomposition
		const int binary = iteration == num_iters ? 0 : (z.y() > 0);

		const vec4f colours[2] = { vec4f(160, 100, 200, 256) / 256, vec4f(137, 25, 100, 256) / 256 };
		const vec4f col_out = colours[iteration % 2] * binary;
		return col_out * col_out * 3.0f;
	}
}


inline double sign(double v) { return (v == 0) ? 0 : (v > 0) ? 1.0 : -1.0; }

// Convert uniform distribution into triangle-shaped distribution, from https://www.shadertoy.com/view/4t2SDh
inline double triDist(double v)
{
	const double orig = v * 2 - 1;
	v = orig / std::sqrt(std::abs(orig));
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


void Hilbert(const vec2i dx, const vec2i dy, vec2i p, int size, std::vector<int> & ordering_out)
{
	if (size > 1)
	{
		size >>= 1;
		Hilbert( dy,  dx, p, size, ordering_out); p += dy * size;
		Hilbert( dx,  dy, p, size, ordering_out); p += dx * size;
		Hilbert( dx,  dy, p, size, ordering_out); p += dx * (size - 1) - dy;
		Hilbert(-dy, -dx, p, size, ordering_out);
	}
	else ordering_out.push_back(p.y() * noise_size + p.x());
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
		const int bucket_i = counter->fetch_add(1);
		if (bucket_i >= num_buckets)
			break;

		// Get pixel ranges for current bucket
		const int bucket_y  = bucket_i / x_buckets;
		const int bucket_x  = bucket_i - x_buckets * bucket_y;
		const int bucket_x0 = bucket_x * bucket_size, bucket_x1 = std::min(bucket_x0 + bucket_size, xres);
		const int bucket_y0 = bucket_y * bucket_size, bucket_y1 = std::min(bucket_y0 + bucket_size, yres);

		for (int y = bucket_y0; y < bucket_y1; ++y)
		for (int x = bucket_x0; x < bucket_x1; ++x)
		{
			const double n = noise[(y % noise_size) * noise_size + (x % noise_size)] * (1.0 / 65536);

			vec4f sum = 0;
			for (int s = 0; s < num_samples; ++s)
			{
				double i = s * inv_num_samples + n; i = (i < 1) ? i : i - 1;
				double j = samples[s].x()      + n; j = (j < 1) ? j : j - 1;
				double k = samples[s].y()      + n; k = (k < 1) ? k : k - 1;

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
	const int num_threads = 1;
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
		Hilbert(vec2i(1, 0), vec2i(0, 1), 0, noise_size, hilbert_vals);

		uint64_t v = 0;
		for (int i = 0; i < noise_size * noise_size; ++i)
		{
			v += 11400714819323198487ull;
			noise[hilbert_vals[i]] = (uint16_t)(v >> 48);
		}
	}

	std::vector<std::thread> render_threads(num_threads);

	const auto bench_start = std::chrono::system_clock::now();

	for (int f = 0; f < num_frames; ++f)
	{
		const auto t_start = std::chrono::system_clock::now();
		{
			std::atomic<int> counter = { 0 };
			for (int z = 0; z < num_threads; ++z) render_threads[z] = std::thread(RenderThreadFunc, f, &samples[0], &noise[0], &counter, &image[0]);
			for (int z = 0; z < num_threads; ++z) render_threads[z].join();
		}
		const std::chrono::duration<double> elapsed_time = std::chrono::system_clock::now() - t_start;

		char filename[256];
		sprintf(filename, "%s/frames/frame%04d.png", dir_prefix, f);
		if (save_frames) stbi_write_png(filename, xres, yres, 4, &image[0], xres * 4);

		printf("Frame %d took %.2f seconds\n", f, elapsed_time.count());
	}
	printf("\n");

	const std::chrono::duration<double> elapsed_time = std::chrono::system_clock::now() - bench_start;
	printf("Rendering animation took %.2f seconds\n", elapsed_time.count());

	return 0;
}
