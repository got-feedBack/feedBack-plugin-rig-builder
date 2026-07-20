#ifndef RB_SHARED_ANALOG_MULTIPLIER_HPP
#define RB_SHARED_ANALOG_MULTIPLIER_HPP

#include <cmath>

namespace rbcomponents {

// Motorola MC1495 four-quadrant multiplier. Inputs and output are volts. The
// resistor values are the external axis scaling used by the surrounding
// circuit; the transfer keeps the datasheet's 0.1/V nominal scale factor and
// adds the gradual Gilbert-cell compression that appears outside its linear
// +/-10 V input region.
class Mc1495Multiplier
{
    float xInputROhm = 18000.0f;
    float yInputROhm = 47000.0f;
    float outputScale = 1.0f;

    static float axis(float volts, float inputROhm)
    {
        const float scaled = volts * (15000.0f / inputROhm);
        const float knee = 10.0f;
        return knee * std::tanh(scaled / knee);
    }

public:
    void setAxisResistors(float xOhm, float yOhm)
    {
        xInputROhm = xOhm > 100.0f ? xOhm : 100.0f;
        yInputROhm = yOhm > 100.0f ? yOhm : 100.0f;
    }

    void setOutputScale(float scale)
    {
        outputScale = scale;
    }

    float process(float xVolts, float yVolts) const
    {
        return outputScale * 0.1f * axis(xVolts, xInputROhm)
             * axis(yVolts, yInputROhm);
    }
};

} // namespace rbcomponents

#endif // RB_SHARED_ANALOG_MULTIPLIER_HPP
