#include "stdafx.h"
#include "Render/SunDirection.h"

#include <cmath>
#include <algorithm>

namespace BloodlustMU
{
    SunDirection g_SunDirection;

    void GetShadowShear(float& shearX, float& shearY)
    {
        const float hx = g_SunDirection.x;
        const float hy = g_SunDirection.y;
        const float hz = g_SunDirection.z;

        const float horiz = std::sqrt(hx * hx + hy * hy);
        if (horiz < 1e-4f || hz <= 1e-4f)
        {
            // Degenerate sun (straight up / below ground): legacy look.
            shearX = -0.5f;
            shearY = 0.0f;
            return;
        }

        // Shadow length per unit height = cot(elevation) = horizontal / up.
        float slope = horiz / hz;
        slope = std::min(std::max(slope, 0.05f), 3.0f);   // keep shadows sane

        // Shadows fall OPPOSITE the sun's horizontal direction.
        shearX = -(hx / horiz) * slope;
        shearY = -(hy / horiz) * slope;
    }
}
