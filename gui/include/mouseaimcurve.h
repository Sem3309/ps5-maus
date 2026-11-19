// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Pure math for turning mouse movement into analog stick deflection.
// Kept header-only and Qt-free so it can be unit-tested on its own.

#ifndef CHIAKI_MOUSEAIMCURVE_H
#define CHIAKI_MOUSEAIMCURVE_H

#include <cmath>
#include <cstdint>
#include <deque>
#include <algorithm>

struct MouseAimParams
{
	// counts per second (mouse "dots") that give full stick deflection at sensitivity 1.0
	double full_speed_counts = 1500.0;
	double sensitivity = 1.0;      // multiplier, higher = faster camera
	double y_ratio = 1.0;          // vertical sensitivity relative to horizontal
	double exponent = 0.75;        // <1 boosts small movements, >1 gives more precision
	double deadzone = 0.12;        // game's inner stick deadzone to jump over (0..0.5)
	double smoothing_ms = 14.0;    // velocity averaging window
	bool invert_y = false;
};

struct StickOut
{
	int16_t x = 0;
	int16_t y = 0;
};

class MouseAimCurve
{
	public:
		void AddMovement(double t_ms, double dx, double dy)
		{
			samples.push_back({t_ms, dx, dy});
		}

		void Reset() { samples.clear(); }

		// Compute stick output at time t_ms.
		StickOut Compute(double t_ms, const MouseAimParams &p)
		{
			const double window = std::max(4.0, p.smoothing_ms);
			while(!samples.empty() && samples.front().t < t_ms - window)
				samples.pop_front();

			double sx = 0.0, sy = 0.0;
			for(const auto &s : samples)
			{
				sx += s.dx;
				sy += s.dy;
			}
			StickOut out;
			if(sx == 0.0 && sy == 0.0)
				return out;

			// counts per second
			const double vx = sx * 1000.0 / window;
			const double vy = sy * 1000.0 / window * p.y_ratio * (p.invert_y ? -1.0 : 1.0);
			const double full = std::max(1.0, p.full_speed_counts) / std::max(0.01, p.sensitivity);

			const double nx = vx / full;
			const double ny = vy / full;
			const double mag = std::sqrt(nx * nx + ny * ny);
			if(mag < 1e-6)
				return out;

			const double dz = std::clamp(p.deadzone, 0.0, 0.9);
			const double curved = std::pow(std::min(1.0, mag), std::max(0.1, p.exponent));
			const double outmag = dz + (1.0 - dz) * curved;

			double ox = nx / mag * outmag;
			double oy = ny / mag * outmag;
			ox = std::clamp(ox, -1.0, 1.0);
			oy = std::clamp(oy, -1.0, 1.0);
			out.x = static_cast<int16_t>(std::lround(ox * 32767.0));
			out.y = static_cast<int16_t>(std::lround(oy * 32767.0));
			return out;
		}

	private:
		struct Sample { double t, dx, dy; };
		std::deque<Sample> samples;
};

#endif // CHIAKI_MOUSEAIMCURVE_H
