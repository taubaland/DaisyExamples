// Script interface for Djin. A script turns the (conformed) stereo input into a
// stereo "wet" signal. The shared chain (blend, tape, compressor, limiter,
// volume) is applied outside, in Djin.cpp.
#pragma once
#include <cstddef>

namespace djin
{
class Script
{
  public:
    virtual ~Script() {}

    /** One-time setup. */
    virtual void Init(float sr) { (void)sr; }
    /** Called each time the script is loaded; reset all state. */
    virtual void Activate() {}
    /** Main encoder (the one with teeth), -1/+1 per detent. */
    virtual void MainEncoder(int inc) { (void)inc; }
    /** Length encoder (the menu encoder while a script runs). */
    virtual void LengthEncoder(int inc) { (void)inc; }
    /** Block process. Inputs and outputs are separate L/R arrays. */
    virtual void Process(const float* in_l,
                         const float* in_r,
                         float*       out_l,
                         float*       out_r,
                         size_t       n)
        = 0;
    /** True once per grain/slice start; drives the LED flicker. */
    virtual bool TakeFlash() { return false; }
};

/** Placeholder for scripts that are not written yet: passes audio straight through. */
class PassthroughScript : public Script
{
  public:
    void Process(const float* in_l,
                 const float* in_r,
                 float*       out_l,
                 float*       out_r,
                 size_t       n) override
    {
        for(size_t i = 0; i < n; i++)
        {
            out_l[i] = in_l[i];
            out_r[i] = in_r[i];
        }
    }
};
} // namespace djin
