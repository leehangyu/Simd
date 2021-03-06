/*
* Simd Library (http://ermig1979.github.io/Simd).
*
* Copyright (c) 2011-2019 Yermalayeu Ihar.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include "Simd/SimdMemory.h"
#include "Simd/SimdStore.h"
#include "Simd/SimdSynet.h"
#include "Simd/SimdNeon.h"
#include "Simd/SimdBase.h"
#include "Simd/SimdExtract.h"
#include "Simd/SimdPow.h"
#include "Simd/SimdExp.h"

namespace Simd
{
#ifdef SIMD_NEON_ENABLE  
    namespace Neon
    {
        template <bool align> SIMD_INLINE void SynetAddBias(const float * bias, float * dst)
        {
            Store<align>(dst, vaddq_f32(Load<align>(dst), Load<align>(bias)));
        }

        template <bool align> SIMD_INLINE void SynetAddBias(float32x4_t bias, float * dst)
        {
            Store<align>(dst, vaddq_f32(Load<align>(dst), bias));
        }

        template <bool align> SIMD_INLINE void SynetAddBias(const float * bias, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (align)
                assert(((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(bias) : Aligned(size)) && Aligned(dst));
            if ((trans || size == 1) && count != 1)
            {
                size_t aligned = AlignLo(count, QF);
                size_t partial = AlignLo(count, F);
                for (size_t j = 0; j < size; ++j)
                {
                    size_t i = 0;
                    if (partial)
                    {
                        for (; i < aligned; i += QF)
                        {
                            SynetAddBias<align>(bias + i + F * 0, dst + i + F * 0);
                            SynetAddBias<align>(bias + i + F * 1, dst + i + F * 1);
                            SynetAddBias<align>(bias + i + F * 2, dst + i + F * 2);
                            SynetAddBias<align>(bias + i + F * 3, dst + i + F * 3);
                        }
                        for (; i < partial; i += F)
                            SynetAddBias<align>(bias + i, dst + i);
                    }
                    for (; i < count; ++i)
                        dst[i] += bias[i];
                    dst += count;
                }
            }
            else
            {
                size_t aligned = AlignLo(size, QF);
                size_t partial = AlignLo(size, F);
                for (size_t i = 0; i < count; ++i)
                {
                    size_t j = 0;
                    if (partial)
                    {
                        float32x4_t _bias = vdupq_n_f32(bias[i]);
                        for (; j < aligned; j += QF)
                        {
                            SynetAddBias<align>(_bias, dst + j + F * 0);
                            SynetAddBias<align>(_bias, dst + j + F * 1);
                            SynetAddBias<align>(_bias, dst + j + F * 2);
                            SynetAddBias<align>(_bias, dst + j + F * 3);
                        }
                        for (; j < partial; j += F)
                            SynetAddBias<align>(_bias, dst + j);
                    }
                    for (; j < size; ++j)
                        dst[j] += bias[i];
                    dst += size;
                }
            }
        }

        void SynetAddBias(const float * bias, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(bias) : Aligned(size)) && Aligned(dst))
                SynetAddBias<true>(bias, count, size, dst, trans);
            else
                SynetAddBias<false>(bias, count, size, dst, trans);
        }

        template <SimdSynetEltwiseOperationType type> float32x4_t SynetEltwiseLayerForward(float32x4_t src0, float32x4_t src1);

        template <> SIMD_INLINE float32x4_t SynetEltwiseLayerForward<SimdSynetEltwiseOperationProduct>(float32x4_t src0, float32x4_t src1)
        {
            return vmulq_f32(src0, src1);
        }

        template <> SIMD_INLINE float32x4_t SynetEltwiseLayerForward<SimdSynetEltwiseOperationMax>(float32x4_t src0, float32x4_t src1)
        {
            return vmaxq_f32(src0, src1);
        }

        template <> SIMD_INLINE float32x4_t SynetEltwiseLayerForward<SimdSynetEltwiseOperationMin>(float32x4_t src0, float32x4_t src1)
        {
            return vminq_f32(src0, src1);
        }

        template <SimdSynetEltwiseOperationType type, bool align> SIMD_INLINE void SynetEltwiseLayerForward(const float * src0, const float * src1, float * dst, size_t offset)
        {
            Store<align>(dst + offset, SynetEltwiseLayerForward<type>(Load<align>(src0 + offset), Load<align>(src1 + offset)));
        }

        template <SimdSynetEltwiseOperationType type, bool align> void SynetEltwiseLayerForward(float const * const * src, size_t count, size_t size, float * dst)
        {
            size_t aligned = AlignLo(size, QF);
            size_t partial = AlignLo(size, F);
            const float * src0 = src[0];
            const float * src1 = src[1];
            size_t j = 0;
            if (partial)
            {
                for (; j < aligned; j += QF)
                {
                    SynetEltwiseLayerForward<type, align>(src0, src1, dst, j + F * 0);
                    SynetEltwiseLayerForward<type, align>(src0, src1, dst, j + F * 1);
                    SynetEltwiseLayerForward<type, align>(src0, src1, dst, j + F * 2);
                    SynetEltwiseLayerForward<type, align>(src0, src1, dst, j + F * 3);
                }
                for (; j < partial; j += F)
                    SynetEltwiseLayerForward<type, align>(src0, src1, dst, j);
            }
            for (; j < size; ++j)
                dst[j] = Base::SynetEltwiseLayerForward<type>(src0[j], src1[j]);
            for (size_t i = 2; i < count; ++i)
            {
                const float * srci = src[i];
                size_t j = 0;
                if (partial)
                {
                    for (; j < aligned; j += QF)
                    {
                        SynetEltwiseLayerForward<type, align>(dst, srci, dst, j + F * 0);
                        SynetEltwiseLayerForward<type, align>(dst, srci, dst, j + F * 1);
                        SynetEltwiseLayerForward<type, align>(dst, srci, dst, j + F * 2);
                        SynetEltwiseLayerForward<type, align>(dst, srci, dst, j + F * 3);
                    }
                    for (; j < partial; j += F)
                        SynetEltwiseLayerForward<type, align>(dst, srci, dst, j);
                }
                for (; j < size; ++j)
                    dst[j] = Base::SynetEltwiseLayerForward<type>(dst[j], srci[j]);
            }
        }

        template <bool align> SIMD_INLINE void SynetEltwiseLayerForwardSum(const float * src0, const float32x4_t & weight0, const float * src1, const float32x4_t & weight1, float * dst, size_t offset)
        {
            Store<align>(dst + offset, vmlaq_f32(vmulq_f32(Load<align>(src0 + offset), weight0), Load<align>(src1 + offset), weight1));
        }

        template <bool align> SIMD_INLINE void SynetEltwiseLayerForwardSum(const float * src, const float32x4_t & weight, float * dst, size_t offset)
        {
            Store<align>(dst + offset, vmlaq_f32(Load<align>(dst + offset), Load<align>(src + offset), weight));
        }

        template <bool align> void SynetEltwiseLayerForwardSum(float const * const * src, const float * weight, size_t count, size_t size, float * dst)
        {
            size_t aligned = AlignLo(size, QF);
            size_t partial = AlignLo(size, F);
            const float * src0 = src[0];
            const float * src1 = src[1];
            float32x4_t weight0 = vdupq_n_f32(weight[0]);
            float32x4_t weight1 = vdupq_n_f32(weight[1]);
            size_t j = 0;
            if (partial)
            {
                for (; j < aligned; j += QF)
                {
                    SynetEltwiseLayerForwardSum<align>(src0, weight0, src1, weight1, dst, j + F * 0);
                    SynetEltwiseLayerForwardSum<align>(src0, weight0, src1, weight1, dst, j + F * 1);
                    SynetEltwiseLayerForwardSum<align>(src0, weight0, src1, weight1, dst, j + F * 2);
                    SynetEltwiseLayerForwardSum<align>(src0, weight0, src1, weight1, dst, j + F * 3);
                }
                for (; j < partial; j += F)
                    SynetEltwiseLayerForwardSum<align>(src0, weight0, src1, weight1, dst, j);
            }
            for (; j < size; ++j)
                dst[j] = src0[j] * weight[0] + src1[j] * weight[1];
            for (size_t i = 2; i < count; ++i)
            {
                const float * srci = src[i];
                float32x4_t weighti = vdupq_n_f32(weight[i]);
                size_t j = 0;
                if (partial)
                {
                    for (; j < aligned; j += QF)
                    {
                        SynetEltwiseLayerForwardSum<align>(srci, weighti, dst, j + F * 0);
                        SynetEltwiseLayerForwardSum<align>(srci, weighti, dst, j + F * 1);
                        SynetEltwiseLayerForwardSum<align>(srci, weighti, dst, j + F * 2);
                        SynetEltwiseLayerForwardSum<align>(srci, weighti, dst, j + F * 3);
                    }
                    for (; j < partial; j += F)
                        SynetEltwiseLayerForwardSum<align>(srci, weighti, dst, j);
                }
                for (; j < size; ++j)
                    dst[j] += srci[j] * weight[i];
            }
        }

        template <bool align> void SynetEltwiseLayerForward(float const * const * src, const float * weight, size_t count, size_t size, SimdSynetEltwiseOperationType type, float * dst)
        {
            switch (type)
            {
            case SimdSynetEltwiseOperationProduct:
                SynetEltwiseLayerForward<SimdSynetEltwiseOperationProduct, align>(src, count, size, dst);
                break;
            case SimdSynetEltwiseOperationSum:
                SynetEltwiseLayerForwardSum<align>(src, weight, count, size, dst);
                break;
            case SimdSynetEltwiseOperationMax:
                SynetEltwiseLayerForward<SimdSynetEltwiseOperationMax, align>(src, count, size, dst);
                break;
            case SimdSynetEltwiseOperationMin:
                SynetEltwiseLayerForward<SimdSynetEltwiseOperationMin, align>(src, count, size, dst);
                break;
            default:
                assert(0);
            }
        }

        void SynetEltwiseLayerForward(float const * const * src, const float * weight, size_t count, size_t size, SimdSynetEltwiseOperationType type, float * dst)
        {
            assert(count >= 2);
            bool aligned = Aligned(dst) && Aligned(src[0]) && Aligned(src[1]);
            for (size_t i = 2; i < count; ++i)
                aligned = aligned && Aligned(src[i]);
            if (aligned)
                SynetEltwiseLayerForward<true>(src, weight, count, size, type, dst);
            else
                SynetEltwiseLayerForward<false>(src, weight, count, size, type, dst);
        }

        template <bool align> SIMD_INLINE void SynetFusedLayerForward0(const float * src, const float * bias, const float * scale, float * dst, size_t offset)
        {
            float32x4_t _bias = Load<align>(bias + offset);
            float32x4_t x = vaddq_f32(Load<align>(src + offset), _bias);
            float32x4_t _scale = Load<align>(scale + offset);
            Store<align>(dst + offset, vmlaq_f32(vmaxq_f32(vdupq_n_f32(0.0f), x), vsubq_f32(x, vabsq_f32(x)), _scale));
        }

        template <bool align> SIMD_INLINE void SynetFusedLayerForward0(const float * src, float32x4_t bias, float32x4_t scale, float * dst, size_t offset)
        {
            float32x4_t x = vaddq_f32(Load<align>(src + offset), bias);
            Store<align>(dst + offset, vmlaq_f32(vmaxq_f32(vdupq_n_f32(0.0f), x), vsubq_f32(x, vabsq_f32(x)), scale));
        }

        template <bool align> void SynetFusedLayerForward0(const float * src, const float * bias, const float * scale, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (align)
                assert(((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(scale) && Aligned(bias) : Aligned(size)) && Aligned(src) && Aligned(dst));
            if ((trans || size == 1) && count != 1)
            {
                size_t aligned = AlignLo(count, QF);
                size_t partial = AlignLo(count, F);
                for (size_t j = 0; j < size; ++j)
                {
                    size_t i = 0;
                    if (partial)
                    {
                        for (; i < aligned; i += QF)
                        {
                            SynetFusedLayerForward0<align>(src, bias, scale, dst, i + 0 * F);
                            SynetFusedLayerForward0<align>(src, bias, scale, dst, i + 1 * F);
                            SynetFusedLayerForward0<align>(src, bias, scale, dst, i + 2 * F);
                            SynetFusedLayerForward0<align>(src, bias, scale, dst, i + 3 * F);
                        }
                        for (; i < partial; i += F)
                            SynetFusedLayerForward0<align>(src, bias, scale, dst, i);
                    }
                    for (; i < count; ++i)
                        dst[i] = Base::SynetFusedLayerForward0(src[i] + bias[i], scale[i]);
                    src += count;
                    dst += count;
                }
            }
            else
            {
                size_t aligned = AlignLo(size, QF);
                size_t partial = AlignLo(size, F);
                for (size_t i = 0; i < count; ++i)
                {
                    size_t j = 0;
                    if (partial)
                    {
                        float32x4_t _bias = vdupq_n_f32(bias[i]);
                        float32x4_t _scale = vdupq_n_f32(scale[i]);
                        for (; j < aligned; j += QF)
                        {
                            SynetFusedLayerForward0<align>(src, _bias, _scale, dst, j + 0 * F);
                            SynetFusedLayerForward0<align>(src, _bias, _scale, dst, j + 1 * F);
                            SynetFusedLayerForward0<align>(src, _bias, _scale, dst, j + 2 * F);
                            SynetFusedLayerForward0<align>(src, _bias, _scale, dst, j + 3 * F);
                        }
                        for (; j < partial; j += F)
                            SynetFusedLayerForward0<align>(src, _bias, _scale, dst, j);
                    }
                    for (; j < size; ++j)
                        dst[j] = Base::SynetFusedLayerForward0(src[j] + bias[i], scale[i]);
                    src += size;
                    dst += size;
                }
            }
        }

        void SynetFusedLayerForward0(const float * src, const float * bias, const float * scale, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(scale) && Aligned(bias) : Aligned(size)) && Aligned(src) && Aligned(dst))
                SynetFusedLayerForward0<true>(src, bias, scale, count, size, dst, trans);
            else
                SynetFusedLayerForward0<false>(src, bias, scale, count, size, dst, trans);
        }


        template <bool align> SIMD_INLINE void SynetFusedLayerForward1(const float * src, const float * bias0, const float * scale1, const float *  bias1, float32x4_t _0, float * dst, size_t offset)
        {
            float32x4_t _bias0 = Load<align>(bias0 + offset);
            float32x4_t x = vaddq_f32(Load<align>(src + offset), _bias0);
            float32x4_t _scale1 = Load<align>(scale1 + offset);
            float32x4_t _bias1 = Load<align>(bias1 + offset);
            Store<align>(dst + offset, vaddq_f32(vmlaq_f32(_bias1, vmaxq_f32(_0, vnegq_f32(x)), _scale1), vmaxq_f32(_0, x)));
        }

        template <bool align> SIMD_INLINE void SynetFusedLayerForward1(const float * src, float32x4_t bias0, float32x4_t scale1, float32x4_t bias1, float32x4_t _0, float * dst, size_t offset)
        {
            float32x4_t x = vaddq_f32(Load<align>(src + offset), bias0);
            Store<align>(dst + offset, vaddq_f32(vmlaq_f32(bias1, vmaxq_f32(_0, vnegq_f32(x)), scale1), vmaxq_f32(_0, x)));
        }

        template <bool align> void SynetFusedLayerForward1(const float * src, const float * bias0, const float * scale1, const float * bias1, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (align)
                assert(((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(bias0) && Aligned(scale1) && Aligned(bias1) : Aligned(size)) && Aligned(src) && Aligned(dst));
            float32x4_t _0 = vdupq_n_f32(0.0f);
            if ((trans || size == 1) && count != 1)
            {
                size_t aligned = AlignLo(count, QF);
                size_t partial = AlignLo(count, F);
                for (size_t j = 0; j < size; ++j)
                {
                    size_t i = 0;
                    if (partial)
                    {
                        for (; i < aligned; i += QF)
                        {
                            SynetFusedLayerForward1<align>(src, bias0, scale1, bias1, _0, dst, i + 0 * F);
                            SynetFusedLayerForward1<align>(src, bias0, scale1, bias1, _0, dst, i + 1 * F);
                            SynetFusedLayerForward1<align>(src, bias0, scale1, bias1, _0, dst, i + 2 * F);
                            SynetFusedLayerForward1<align>(src, bias0, scale1, bias1, _0, dst, i + 3 * F);
                        }
                        for (; i < partial; i += F)
                            SynetFusedLayerForward1<align>(src, bias0, scale1, bias1, _0, dst, i);
                    }
                    for (; i < count; ++i)
                        dst[i] = Base::SynetFusedLayerForward1(src[i] + bias0[i], scale1[i], bias1[i]);
                    src += count;
                    dst += count;
                }
            }
            else
            {
                size_t aligned = AlignLo(size, QF);
                size_t partial = AlignLo(size, F);
                for (size_t i = 0; i < count; ++i)
                {
                    size_t j = 0;
                    if (partial)
                    {
                        float32x4_t _bias0 = vdupq_n_f32(bias0[i]);
                        float32x4_t _scale1 = vdupq_n_f32(scale1[i]);
                        float32x4_t _bias1 = vdupq_n_f32(bias1[i]);
                        for (; j < aligned; j += QF)
                        {
                            SynetFusedLayerForward1<align>(src, _bias0, _scale1, _bias1, _0, dst, j + 0 * F);
                            SynetFusedLayerForward1<align>(src, _bias0, _scale1, _bias1, _0, dst, j + 1 * F);
                            SynetFusedLayerForward1<align>(src, _bias0, _scale1, _bias1, _0, dst, j + 2 * F);
                            SynetFusedLayerForward1<align>(src, _bias0, _scale1, _bias1, _0, dst, j + 3 * F);
                        }
                        for (; j < partial; j += F)
                            SynetFusedLayerForward1<align>(src, _bias0, _scale1, _bias1, _0, dst, j);
                    }
                    for (; j < size; ++j)
                        dst[j] = Base::SynetFusedLayerForward1(src[j] + bias0[i], scale1[i], bias1[i]);
                    src += size;
                    dst += size;
                }
            }
        }

        void SynetFusedLayerForward1(const float * src, const float * bias0, const float * scale1, const float * bias1, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(bias0) && Aligned(scale1) && Aligned(bias1) : Aligned(size)) && Aligned(src) && Aligned(dst))
                SynetFusedLayerForward1<true>(src, bias0, scale1, bias1, count, size, dst, trans);
            else
                SynetFusedLayerForward1<false>(src, bias0, scale1, bias1, count, size, dst, trans);
        }

        template <bool align> SIMD_INLINE void SynetFusedLayerForward2(const float * src, const float * scale, const float * bias, float32x4_t slope, float32x4_t _0, float * dst, size_t offset)
        {
            float32x4_t _src = Load<align>(src + offset);
            float32x4_t _scale = Load<align>(scale + offset);
            float32x4_t _bias = Load<align>(bias + offset);
            float32x4_t x = vmlaq_f32(_bias, _src, _scale);
            Store<align>(dst + offset, vmlaq_f32(vmaxq_f32(_0, x), vminq_f32(_0, x), slope));
        }

        template <bool align> SIMD_INLINE void SynetFusedLayerForward2(const float * src, float32x4_t scale, float32x4_t bias, float32x4_t slope, float32x4_t _0, float * dst, size_t offset)
        {
            float32x4_t _src = Load<align>(src + offset);
            float32x4_t x = vmlaq_f32(bias, _src, scale);
            Store<align>(dst + offset, vmlaq_f32(vmaxq_f32(_0, x), vminq_f32(_0, x), slope));
        }

        template <bool align> void SynetFusedLayerForward2(const float * src, const float * scale, const float * bias, size_t count, size_t size, const float * slope, float * dst, SimdBool trans)
        {
            if (align)
                assert(((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(scale) && Aligned(bias) : Aligned(size)) && Aligned(src) && Aligned(dst));
            float32x4_t _slope = vdupq_n_f32(slope[0]);
            float32x4_t _0 = vdupq_n_f32(0.0f);
            if ((trans || size == 1) && count != 1)
            {
                size_t aligned = AlignLo(count, QF);
                size_t partial = AlignLo(count, F);
                for (size_t j = 0; j < size; ++j)
                {
                    size_t i = 0;
                    if (partial)
                    {
                        for (; i < aligned; i += QF)
                        {
                            SynetFusedLayerForward2<align>(src, scale, bias, _slope, _0, dst, i + 0 * F);
                            SynetFusedLayerForward2<align>(src, scale, bias, _slope, _0, dst, i + 1 * F);
                            SynetFusedLayerForward2<align>(src, scale, bias, _slope, _0, dst, i + 2 * F);
                            SynetFusedLayerForward2<align>(src, scale, bias, _slope, _0, dst, i + 3 * F);
                        }
                        for (; i < partial; i += F)
                            SynetFusedLayerForward2<align>(src, scale, bias, _slope, _0, dst, i);
                    }
                    for (; i < count; ++i)
                        dst[i] = Base::SynetFusedLayerForward2(src[i], scale[i], bias[i], slope[0]);
                    src += count;
                    dst += count;
                }
            }
            else
            {
                size_t aligned = AlignLo(size, QF);
                size_t partial = AlignLo(size, F);
                for (size_t i = 0; i < count; ++i)
                {
                    size_t j = 0;
                    if (partial)
                    {
                        float32x4_t _scale = vdupq_n_f32(scale[i]);
                        float32x4_t _bias = vdupq_n_f32(bias[i]);
                        for (; j < aligned; j += QF)
                        {
                            SynetFusedLayerForward2<align>(src, _scale, _bias, _slope, _0, dst, j + 0 * F);
                            SynetFusedLayerForward2<align>(src, _scale, _bias, _slope, _0, dst, j + 1 * F);
                            SynetFusedLayerForward2<align>(src, _scale, _bias, _slope, _0, dst, j + 2 * F);
                            SynetFusedLayerForward2<align>(src, _scale, _bias, _slope, _0, dst, j + 3 * F);
                        }
                        for (; j < partial; j += F)
                            SynetFusedLayerForward2<align>(src, _scale, _bias, _slope, _0, dst, j);
                    }
                    for (; j < size; ++j)
                        dst[j] = Base::SynetFusedLayerForward2(src[j], scale[i], bias[i], slope[0]);
                    src += size;
                    dst += size;
                }
            }
        }

        void SynetFusedLayerForward2(const float * src, const float * scale, const float * bias, size_t count, size_t size, const float * slope, float * dst, SimdBool trans)
        {
            if (((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(scale) && Aligned(bias) : Aligned(size)) && Aligned(src) && Aligned(dst))
                SynetFusedLayerForward2<true>(src, scale, bias, count, size, slope, dst, trans);
            else
                SynetFusedLayerForward2<false>(src, scale, bias, count, size, slope, dst, trans);
        }

        template <bool align> SIMD_INLINE void SynetFusedLayerForward3(const float * src, const float * bias, const float * scale, float32x4_t _0, float * dst, size_t offset)
        {
            float32x4_t _bias = Load<align>(bias + offset);
            float32x4_t x = vaddq_f32(Load<align>(src + offset), _bias);
            float32x4_t _scale = Load<align>(scale + offset);
            float32x4_t pos = vmaxq_f32(_0, x);
            float32x4_t neg = vminq_f32(_0, x);
            Store<align>(dst + offset, vmlaq_f32(pos, _scale, neg));
        }

        template <bool align> SIMD_INLINE void SynetFusedLayerForward3(const float * src, float32x4_t bias, float32x4_t scale, float32x4_t _0, float * dst, size_t offset)
        {
            float32x4_t x = vaddq_f32(Load<align>(src + offset), bias);
            float32x4_t pos = vmaxq_f32(_0, x);
            float32x4_t neg = vminq_f32(_0, x);
            Store<align>(dst + offset, vmlaq_f32(pos, scale, neg));
        }

        template <bool align> void SynetFusedLayerForward3(const float * src, const float * bias, const float * scale, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (align)
                assert(((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(scale) && Aligned(bias) : Aligned(size)) && Aligned(src) && Aligned(dst));
            float32x4_t _0 = vdupq_n_f32(0.0f);
            if ((trans || size == 1) && count != 1)
            {
                size_t aligned = AlignLo(count, QF);
                size_t partial = AlignLo(count, F);
                for (size_t j = 0; j < size; ++j)
                {
                    size_t i = 0;
                    if (partial)
                    {
                        for (; i < aligned; i += QF)
                        {
                            SynetFusedLayerForward3<align>(src, bias, scale, _0, dst, i + 0 * F);
                            SynetFusedLayerForward3<align>(src, bias, scale, _0, dst, i + 1 * F);
                            SynetFusedLayerForward3<align>(src, bias, scale, _0, dst, i + 2 * F);
                            SynetFusedLayerForward3<align>(src, bias, scale, _0, dst, i + 3 * F);
                        }
                        for (; i < partial; i += F)
                            SynetFusedLayerForward3<align>(src, bias, scale, _0, dst, i);
                    }
                    for (; i < count; ++i)
                        dst[i] = Base::SynetFusedLayerForward3(src[i] + bias[i], scale[i]);
                    src += count;
                    dst += count;
                }
            }
            else
            {
                size_t aligned = AlignLo(size, QF);
                size_t partial = AlignLo(size, F);
                for (size_t i = 0; i < count; ++i)
                {
                    size_t j = 0;
                    if (partial)
                    {
                        float32x4_t _bias = vdupq_n_f32(bias[i]);
                        float32x4_t _scale = vdupq_n_f32(scale[i]);
                        for (; j < aligned; j += QF)
                        {
                            SynetFusedLayerForward3<align>(src, _bias, _scale, _0, dst, j + 0 * F);
                            SynetFusedLayerForward3<align>(src, _bias, _scale, _0, dst, j + 1 * F);
                            SynetFusedLayerForward3<align>(src, _bias, _scale, _0, dst, j + 2 * F);
                            SynetFusedLayerForward3<align>(src, _bias, _scale, _0, dst, j + 3 * F);
                        }
                        for (; j < partial; j += F)
                            SynetFusedLayerForward3<align>(src, _bias, _scale, _0, dst, j);
                    }
                    for (; j < size; ++j)
                        dst[j] = Base::SynetFusedLayerForward3(src[j] + bias[i], scale[i]);
                    src += size;
                    dst += size;
                }
            }
        }

        void SynetFusedLayerForward3(const float * src, const float * bias, const float * scale, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(scale) && Aligned(bias) : Aligned(size)) && Aligned(src) && Aligned(dst))
                SynetFusedLayerForward3<true>(src, bias, scale, count, size, dst, trans);
            else
                SynetFusedLayerForward3<false>(src, bias, scale, count, size, dst, trans);
        }

        template <bool align> SIMD_INLINE void SynetFusedLayerForward4(const float * src, const float * bias0, float32x4_t scale1, float32x4_t bias1, float32x4_t _0, float * dst0, float * dst1, size_t offset)
        {
            float32x4_t x = vaddq_f32(Load<align>(src + offset), Load<align>(bias0 + offset));
            Store<align>(dst0 + offset, vmaxq_f32(_0, x));
            Store<align>(dst1 + offset, vmaxq_f32(_0, vmlaq_f32(bias1, scale1, x)));
        }

        template <bool align> SIMD_INLINE void SynetFusedLayerForward4(const float * src, float32x4_t bias0, float32x4_t scale1, float32x4_t bias1, float32x4_t _0, float * dst0, float * dst1, size_t offset)
        {
            float32x4_t x = vaddq_f32(Load<align>(src + offset), bias0);
            Store<align>(dst0 + offset, vmaxq_f32(_0, x));
            Store<align>(dst1 + offset, vmaxq_f32(_0, vmlaq_f32(bias1, scale1, x)));
        }

        template<bool align> void SynetFusedLayerForward4(const float * src, const float * bias0, const float * scale1, const float * bias1, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (align)
                assert(((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(bias0) : Aligned(size)) && Aligned(src) && Aligned(dst));
            float32x4_t _scale1 = vdupq_n_f32(scale1[0]);
            float32x4_t _bias1 = vdupq_n_f32(bias1[0]);
            float32x4_t _0 = vdupq_n_f32(0.0f);
            if ((trans || size == 1) && count != 1)
            {
                float * dst0 = dst, *dst1 = dst + count;
                size_t aligned = AlignLo(count, QF);
                size_t partial = AlignLo(count, F);
                for (size_t j = 0; j < size; ++j)
                {
                    size_t i = 0;
                    if (partial)
                    {
                        for (; i < aligned; i += QF)
                        {
                            SynetFusedLayerForward4<align>(src, bias0, _scale1, _bias1, _0, dst0, dst1, i + 0 * F);
                            SynetFusedLayerForward4<align>(src, bias0, _scale1, _bias1, _0, dst0, dst1, i + 1 * F);
                            SynetFusedLayerForward4<align>(src, bias0, _scale1, _bias1, _0, dst0, dst1, i + 2 * F);
                            SynetFusedLayerForward4<align>(src, bias0, _scale1, _bias1, _0, dst0, dst1, i + 3 * F);
                        }
                        for (; i < partial; i += F)
                            SynetFusedLayerForward4<align>(src, bias0, _scale1, _bias1, _0, dst0, dst1, i);
                    }
                    for (; i < count; ++i)
                        Base::SynetFusedLayerForward4(src[i], bias0[i], scale1[0], bias1[0], dst0 + i, dst1 + i);
                    src += count;
                    dst0 += 2 * count;
                    dst1 += 2 * count;
                }
            }
            else
            {
                float * dst0 = dst, *dst1 = dst + count * size;
                size_t aligned = AlignLo(size, QF);
                size_t partial = AlignLo(size, F);
                for (size_t i = 0; i < count; ++i)
                {
                    size_t j = 0;
                    if (partial)
                    {
                        float32x4_t _bias0 = vdupq_n_f32(bias0[i]);
                        for (; j < aligned; j += QF)
                        {
                            SynetFusedLayerForward4<align>(src, _bias0, _scale1, _bias1, _0, dst0, dst1, j + 0 * F);
                            SynetFusedLayerForward4<align>(src, _bias0, _scale1, _bias1, _0, dst0, dst1, j + 1 * F);
                            SynetFusedLayerForward4<align>(src, _bias0, _scale1, _bias1, _0, dst0, dst1, j + 2 * F);
                            SynetFusedLayerForward4<align>(src, _bias0, _scale1, _bias1, _0, dst0, dst1, j + 3 * F);
                        }
                        for (; j < partial; j += F)
                            SynetFusedLayerForward4<align>(src, _bias0, _scale1, _bias1, _0, dst0, dst1, j);
                    }
                    for (; j < size; ++j)
                        Base::SynetFusedLayerForward4(src[j], bias0[i], scale1[0], bias1[0], dst0 + j, dst1 + j);
                    src += size;
                    dst0 += size;
                    dst1 += size;
                }
            }
        }

        void SynetFusedLayerForward4(const float * src, const float * bias0, const float * scale1, const float * bias1, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(bias0) : Aligned(size)) && Aligned(src) && Aligned(dst))
                SynetFusedLayerForward4<true>(src, bias0, scale1, bias1, count, size, dst, trans);
            else
                SynetFusedLayerForward4<false>(src, bias0, scale1, bias1, count, size, dst, trans);
        }

        template <bool align> SIMD_INLINE void SynetInnerProductLayerForward(const float * src, const float * weight, size_t offset, float32x4_t & sum)
        {
            float32x4_t s = Load<align>(src + offset);
            float32x4_t w = Load<align>(weight + offset);
            sum = vmlaq_f32(sum, s, w);
        }

        template <bool align> SIMD_INLINE void SynetInnerProductLayerForward(const float * src, const float * weight0, const float * weight1, size_t offset, float32x4_t * sum)
        {
            float32x4_t s = Load<align>(src + offset);
            float32x4_t w0 = Load<align>(weight0 + offset);
            float32x4_t w1 = Load<align>(weight1 + offset);
            sum[0] = vmlaq_f32(sum[0], s, w0);
            sum[1] = vmlaq_f32(sum[1], s, w1);
        }

        template<bool align> void SynetInnerProductLayerForward(const float * src, const float * weight, const float * bias, size_t count, size_t size, float * dst)
        {
            if (align)
                assert(Aligned(src) && Aligned(weight) && Aligned(size) && Aligned(dst));
            size_t count2 = AlignLo(count, 2);
            size_t sizeF = AlignLo(size, F);
            size_t sizeDF = AlignLo(size, DF);
            size_t sizeQF = AlignLo(size, QF);
            size_t i = 0;
            for (; i < count2; i += 2)
            {
                size_t j = 0;
                float sum0 = 0, sum1 = 0;
                const float * weight0 = weight + 0 * size;
                const float * weight1 = weight + 1 * size;
                if (sizeF)
                {
                    float32x4_t sums[4] = { vdupq_n_f32(0.0f), vdupq_n_f32(0.0f), vdupq_n_f32(0.0f), vdupq_n_f32(0.0f) };
                    if (sizeDF)
                    {
                        for (; j < sizeDF; j += DF)
                        {
                            SynetInnerProductLayerForward<align>(src, weight0, weight1, j + 0 * F, sums + 0);
                            SynetInnerProductLayerForward<align>(src, weight0, weight1, j + 1 * F, sums + 2);
                        }
                        sums[0] = vaddq_f32(sums[0], sums[2]);
                        sums[1] = vaddq_f32(sums[1], sums[3]);
                    }
                    for (; j < sizeF; j += F)
                        SynetInnerProductLayerForward<align>(src, weight0, weight1, j, sums);
                    sum0 = ExtractSum32f(sums[0]);
                    sum1 = ExtractSum32f(sums[1]);
                }
                for (; j < size; ++j)
                {
                    sum0 += src[j] * weight0[j];
                    sum1 += src[j] * weight1[j];
                }
                dst[i + 0] = sum0 + (bias ? bias[i + 0] : 0);
                dst[i + 1] = sum1 + (bias ? bias[i + 1] : 0);
                weight += 2*size;
            }
            for (; i < count; ++i)
            {
                size_t j = 0;
                float sum = 0;
                if (sizeF)
                {
                    float32x4_t sums[4] = { vdupq_n_f32(0.0f), vdupq_n_f32(0.0f), vdupq_n_f32(0.0f), vdupq_n_f32(0.0f) };
                    if (sizeQF)
                    {
                        for (; j < sizeQF; j += QF)
                        {
                            SynetInnerProductLayerForward<align>(src, weight, j + 0 * F, sums[0]);
                            SynetInnerProductLayerForward<align>(src, weight, j + 1 * F, sums[1]);
                            SynetInnerProductLayerForward<align>(src, weight, j + 2 * F, sums[2]);
                            SynetInnerProductLayerForward<align>(src, weight, j + 3 * F, sums[3]);
                        }
                        sums[0] = vaddq_f32(vaddq_f32(sums[0], sums[1]), vaddq_f32(sums[2], sums[3]));
                    }
                    for (; j < sizeF; j += F)
                        SynetInnerProductLayerForward<align>(src, weight, j, sums[0]);
                    sum = ExtractSum32f(sums[0]);
                }
                for (; j < size; ++j)
                    sum += src[j] * weight[j];
                dst[i] = sum + (bias ? bias[i] : 0);
                weight += size;
            }
        }

        void SynetInnerProductLayerForward(const float * src, const float * weight, const float * bias, size_t count, size_t size, float * dst)
        {
            if (Aligned(src) && Aligned(weight) && Aligned(size) && Aligned(dst))
                SynetInnerProductLayerForward<true>(src, weight, bias, count, size, dst);
            else
                SynetInnerProductLayerForward<false>(src, weight, bias, count, size, dst);
        }

        template<int shift> SIMD_INLINE float32x4_t LoadAtEdge(const float * src)
        {
            static const int32_t mask[3 * F] = { 0, 0, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0 };
            return And(Load<false>(src + shift), Load<false>((float*)mask + F + shift));
        }

        SIMD_INLINE float32x4_t NoseSquareSum(const float * src)
        {
            float32x4_t s0 = LoadAtEdge<-2>(src);
            float32x4_t s1 = LoadAtEdge<-1>(src);
            float32x4_t s2 = Load<false>(src);
            float32x4_t s3 = Load<false>(src + 1);
            float32x4_t s4 = Load<false>(src + 2);
            return vaddq_f32(vmlaq_f32(vmulq_f32(s0, s0), s1, s1), vmlaq_f32(vmlaq_f32(vmulq_f32(s2, s2), s3, s3), s4, s4));
        }

        SIMD_INLINE float32x4_t BodySquareSum(const float * src)
        {
            float32x4_t s0 = Load<false>(src - 2);
            float32x4_t s1 = Load<false>(src - 1);
            float32x4_t s2 = Load<false>(src);
            float32x4_t s3 = Load<false>(src + 1);
            float32x4_t s4 = Load<false>(src + 2);
            return vaddq_f32(vmlaq_f32(vmulq_f32(s0, s0), s1, s1), vmlaq_f32(vmlaq_f32(vmulq_f32(s2, s2), s3, s3), s4, s4));
        }

        SIMD_INLINE float32x4_t TailSquareSum(const float * src)
        {
            float32x4_t s0 = Load<false>(src - 2);
            float32x4_t s1 = Load<false>(src - 1);
            float32x4_t s2 = Load<false>(src);
            float32x4_t s3 = LoadAtEdge<1>(src);
            float32x4_t s4 = LoadAtEdge<2>(src);
            return vaddq_f32(vmlaq_f32(vmulq_f32(s0, s0), s1, s1), vmlaq_f32(vmlaq_f32(vmulq_f32(s2, s2), s3, s3), s4, s4));
        }

        template <bool align> void SynetLrnLayerCrossChannels(const float * src, size_t half, size_t count, size_t size, const float * k, float * dst, SimdBool trans)
        {
            float32x4_t k0 = vdupq_n_f32(k[0]);
            float32x4_t k1 = vdupq_n_f32(k[1]);
            float32x4_t k2 = vdupq_n_f32(k[2]);
            Pow pow;
            if (trans)
            {
                if (half != 2 || count < F + half)
                {
                    Base::SynetLrnLayerCrossChannels(src, half, count, size, k, dst, trans);
                    return;
                }
                size_t aligned = AlignLo(count - half, F);
                for (size_t j = 0; j < size; ++j)
                {
                    Store<align>(dst + 0, vmulq_f32(Load<align>(src + 0), pow(vmlaq_f32(k0, k1, NoseSquareSum(src + 0)), k2)));
                    for (size_t i = F; i < aligned; i += F)
                        Store<align>(dst + i, vmulq_f32(Load<align>(src + i), pow(vmlaq_f32(k0, k1, BodySquareSum(src + i)), k2)));
                    if (aligned != count - half)
                    {
                        size_t i = count - half - F;
                        Store<false>(dst + i, vmulq_f32(Load<false>(src + i), pow(vmlaq_f32(k0, k1, BodySquareSum(src + i)), k2)));
                    }
                    size_t i = count - F;
                    Store<false>(dst + i, vmulq_f32(Load<false>(src + i), pow(vmlaq_f32(k0, k1, TailSquareSum(src + i)), k2)));
                    src += count;
                    dst += count;
                }
            }
            else
            {
                size_t aligned = AlignLo(size, F);
                Array32f sum(size, true), zero(size, true);

                for (size_t i = 0; i < half; ++i)
                {
                    const float * pos = src + i * size;
                    size_t j = 0;
                    for (; j < aligned; j += F)
                    {
                        float32x4_t _pos = Load<align>(pos + j);
                        Store<true>(sum.data + j, vmlaq_f32(Load<true>(sum.data + j), _pos, _pos));
                    }
                    for (; j < size; ++j)
                        sum[j] += Simd::Square(pos[j]);
                }

                for (size_t i = 0; i < count; ++i)
                {
                    const float * pos = (i < count - half) ? src + half * size : zero.data;
                    const float * neg = (i > half) ? src - (half + 1) * size : zero.data;
                    size_t j = 0;
                    for (; j < aligned; j += F)
                    {
                        float32x4_t _pos = Load<align>(pos + j);
                        float32x4_t _neg = Load<align>(neg + j);
                        float32x4_t _sum = Load<true>(sum.data + j);
                        _sum = vmlsq_f32(vmlaq_f32(_sum, _pos, _pos), _neg, _neg);
                        float32x4_t _src = Load<align>(src + j);
                        Store<true>(sum.data + j, _sum);
                        Store<align>(dst + j, vmulq_f32(_src, pow(vmlaq_f32(k0, k1, _sum), k2)));
                    }
                    for (; j < size; ++j)
                    {
                        sum[j] += Simd::Square(pos[j]);
                        sum[j] -= Simd::Square(neg[j]);
                        dst[j] = src[j] * Base::Pow(k[0] + k[1] * sum[j], k[2]);
                    }
                    src += size;
                    dst += size;
                }
            }
        }

        void SynetLrnLayerCrossChannels(const float * src, size_t half, size_t count, size_t size, const float * k, float * dst, SimdBool trans)
        {
            if (Aligned(src) && Aligned(dst) && Aligned(size))
                SynetLrnLayerCrossChannels<true>(src, half, count, size, k, dst, trans);
            else
                SynetLrnLayerCrossChannels<false>(src, half, count, size, k, dst, trans);
        }

        SIMD_INLINE void PoolingMaxHwc1(const float * src, size_t srcS, size_t srcC, size_t kH, size_t kW, const float32x4_t & min, float * dst)
        {
            float32x4_t max0 = min;
            for (size_t h = 0; h < kH; ++h)
            {
                for (size_t w = 0; w < kW; ++w)
                {
                    max0 = vmaxq_f32(max0, Load<false>(src + w * srcC + 0 * F));
                }
                src += srcS;
            }
            Store<false>(dst + 0 * F, max0);
        }

        SIMD_INLINE void PoolingMaxHwc2(const float * src, size_t srcS, size_t srcC, size_t kH, size_t kW, const float32x4_t & min, float * dst)
        {
            float32x4_t max0 = min;
            float32x4_t max1 = min;
            for (size_t h = 0; h < kH; ++h)
            {
                for (size_t w = 0; w < kW; ++w)
                {
                    max0 = vmaxq_f32(max0, Load<false>(src + w * srcC + 0 * F));
                    max1 = vmaxq_f32(max1, Load<false>(src + w * srcC + 1 * F));
                }
                src += srcS;
            }
            Store<false>(dst + 0 * F, max0);
            Store<false>(dst + 1 * F, max1);
        }

        SIMD_INLINE void PoolingMaxHwc4(const float * src, size_t srcS, size_t srcC, size_t kH, size_t kW, const float32x4_t & min, float * dst)
        {
            float32x4_t max0 = min;
            float32x4_t max1 = min;
            float32x4_t max2 = min;
            float32x4_t max3 = min;
            for (size_t h = 0; h < kH; ++h)
            {
                for (size_t w = 0; w < kW; ++w)
                {
                    max0 = vmaxq_f32(max0, Load<false>(src + w * srcC + 0 * F));
                    max1 = vmaxq_f32(max1, Load<false>(src + w * srcC + 1 * F));
                    max2 = vmaxq_f32(max2, Load<false>(src + w * srcC + 2 * F));
                    max3 = vmaxq_f32(max3, Load<false>(src + w * srcC + 3 * F));
                }
                src += srcS;
            }
            Store<false>(dst + 0 * F, max0);
            Store<false>(dst + 1 * F, max1);
            Store<false>(dst + 2 * F, max2);
            Store<false>(dst + 3 * F, max3);
        }

        SIMD_INLINE void PoolingMaxHwc8(const float * src, size_t srcS, size_t srcC, size_t kH, size_t kW, const float32x4_t & min, float * dst)
        {
            float32x4_t max0 = min;
            float32x4_t max1 = min;
            float32x4_t max2 = min;
            float32x4_t max3 = min;
            float32x4_t max4 = min;
            float32x4_t max5 = min;
            float32x4_t max6 = min;
            float32x4_t max7 = min;
            for (size_t h = 0; h < kH; ++h)
            {
                for (size_t w = 0; w < kW; ++w)
                {
                    max0 = vmaxq_f32(max0, Load<false>(src + w * srcC + 0 * F));
                    max1 = vmaxq_f32(max1, Load<false>(src + w * srcC + 1 * F));
                    max2 = vmaxq_f32(max2, Load<false>(src + w * srcC + 2 * F));
                    max3 = vmaxq_f32(max3, Load<false>(src + w * srcC + 3 * F));
                    max4 = vmaxq_f32(max4, Load<false>(src + w * srcC + 4 * F));
                    max5 = vmaxq_f32(max5, Load<false>(src + w * srcC + 5 * F));
                    max6 = vmaxq_f32(max6, Load<false>(src + w * srcC + 6 * F));
                    max7 = vmaxq_f32(max7, Load<false>(src + w * srcC + 7 * F));
                }
                src += srcS;
            }
            Store<false>(dst + 0 * F, max0);
            Store<false>(dst + 1 * F, max1);
            Store<false>(dst + 2 * F, max2);
            Store<false>(dst + 3 * F, max3);
            Store<false>(dst + 4 * F, max4);
            Store<false>(dst + 5 * F, max5);
            Store<false>(dst + 6 * F, max6);
            Store<false>(dst + 7 * F, max7);
        }

        void SynetPoolingForwardMax(const float * src, size_t srcC, size_t srcH, size_t srcW, size_t kernelY, size_t kernelX,
            size_t strideY, size_t strideX, size_t padY, size_t padX, float * dst, size_t dstH, size_t dstW, SimdBool trans)
        {
            if (trans)
            {
                if (srcC >= F)
                {
                    size_t srcS = srcW * srcC;
                    size_t srcCF1 = AlignLo(srcC, 1 * F);
                    size_t srcCF2 = AlignLo(srcC, 2 * F);
                    size_t srcCF4 = AlignLo(srcC, 4 * F);
                    size_t srcCF8 = AlignLo(srcC, 8 * F);
                    float32x4_t min = vdupq_n_f32(-FLT_MAX);
                    for (size_t ph = 0; ph < dstH; ++ph)
                    {
                        size_t hStart = ph * strideY - padY;
                        size_t hEnd = Simd::Min(hStart + kernelY, srcH);
                        hStart = Simd::Max<ptrdiff_t>(0, hStart);
                        for (size_t pw = 0; pw < dstW; ++pw)
                        {
                            size_t wStart = pw * strideX - padX;
                            size_t wEnd = Simd::Min(wStart + kernelX, srcW);
                            wStart = Simd::Max<ptrdiff_t>(0, wStart);
                            const float * ps = src + hStart * srcS + wStart * srcC;
                            size_t c = 0;
                            for (; c < srcCF8; c += 8 * F)
                                PoolingMaxHwc8(ps + c, srcS, srcC, hEnd - hStart, wEnd - wStart, min, dst + c);
                            for (; c < srcCF4; c += 4 * F)
                                PoolingMaxHwc4(ps + c, srcS, srcC, hEnd - hStart, wEnd - wStart, min, dst + c);
                            for (; c < srcCF2; c += 2 * F)
                                PoolingMaxHwc2(ps + c, srcS, srcC, hEnd - hStart, wEnd - wStart, min, dst + c);
                            for (; c < srcCF1; c += 1 * F)
                                PoolingMaxHwc1(ps + c, srcS, srcC, hEnd - hStart, wEnd - wStart, min, dst + c);
                            if (c < srcC)
                                PoolingMaxHwc1(ps + srcC - F, srcS, srcC, hEnd - hStart, wEnd - wStart, min, dst + srcC - F);
                            dst += srcC;
                        }
                    }
                    return;
                }
            }
            else
            {
                if (strideY == 1 && strideX == 1 && kernelY == 3 && kernelX == 3 && srcH == dstH && srcW == dstW && dstW > F)
                {
                    for (size_t c = 0; c < srcC; ++c, src += srcH * srcW, dst += dstH * dstW)
                        Neon::NeuralPooling1x1Max3x3(src, srcW, srcW, srcH, dst, dstW);
                    return;
                }
                if (strideY == 2 && strideX == 2 && kernelY == 2 && kernelX == 2 && padY == 0 && padX == 0 && dstW >= F)
                {
                    for (size_t c = 0; c < srcC; ++c, src += srcH * srcW, dst += dstH * dstW)
                        Neon::NeuralPooling2x2Max2x2(src, srcW, srcW, srcH, dst, dstW);
                    return;
                }
                if (strideY == 2 && strideX == 2 && kernelY == 3 && kernelX == 3 && padY == 0 && padX == 0 && dstW > F)
                {
                    for (size_t c = 0; c < srcC; ++c, src += srcH * srcW, dst += dstH * dstW)
                        Neon::NeuralPooling2x2Max3x3(src, srcW, srcW, srcH, dst, dstW);
                    return;
                }
            }
            Base::SynetPoolingForwardMax(src, srcC, srcH, srcW, kernelY, kernelX, strideY, strideX, padY, padX, dst, dstH, dstW, trans);
        }

        template <bool align> SIMD_INLINE void SynetPreluLayerForward(const float * src, const float * slope, float32x4_t _0, float * dst, size_t offset)
        {
            Store<align>(dst + offset, SynetPreluLayerForward(Load<align>(src + offset), Load<align>(slope + offset), _0));
        }

        template <bool align> SIMD_INLINE void SynetPreluLayerForward(const float * src, float32x4_t slope, float32x4_t _0, float * dst, size_t offset)
        {
            Store<align>(dst + offset, SynetPreluLayerForward(Load<align>(src + offset), slope, _0));
        }

        template <bool align> void SynetPreluLayerForward(const float * src, const float * slope, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (align)
                assert(((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(slope) : Aligned(size)) && Aligned(src) && Aligned(dst));
            float32x4_t _0 = vdupq_n_f32(0.0f);
            if ((trans || size == 1) && count != 1)
            {
                size_t aligned = AlignLo(count, QF);
                size_t partial = AlignLo(count, F);
                for (size_t j = 0; j < size; ++j)
                {
                    size_t i = 0;
                    if (partial)
                    {
                        for (; i < aligned; i += QF)
                        {
                            SynetPreluLayerForward<align>(src, slope, _0, dst, i + F * 0);
                            SynetPreluLayerForward<align>(src, slope, _0, dst, i + F * 1);
                            SynetPreluLayerForward<align>(src, slope, _0, dst, i + F * 2);
                            SynetPreluLayerForward<align>(src, slope, _0, dst, i + F * 3);
                        }
                        for (; i < partial; i += F)
                            SynetPreluLayerForward<align>(src, slope, _0, dst, i);
                    }
                    for (; i < count; ++i)
                        dst[i] = Base::SynetPreluLayerForward(src[i], slope[i]);
                    src += count;
                    dst += count;
                }
            }
            else
            {
                size_t aligned = AlignLo(size, QF);
                size_t partial = AlignLo(size, F);
                for (size_t i = 0; i < count; ++i)
                {
                    size_t j = 0;
                    if (partial)
                    {
                        float32x4_t _slope = vdupq_n_f32(slope[i]);
                        for (; j < aligned; j += QF)
                        {
                            SynetPreluLayerForward<align>(src, _slope, _0, dst, j + F * 0);
                            SynetPreluLayerForward<align>(src, _slope, _0, dst, j + F * 1);
                            SynetPreluLayerForward<align>(src, _slope, _0, dst, j + F * 2);
                            SynetPreluLayerForward<align>(src, _slope, _0, dst, j + F * 3);
                        }
                        for (; j < partial; j += F)
                            SynetPreluLayerForward<align>(src, _slope, _0, dst, j);
                    }
                    for (; j < size; ++j)
                        dst[j] = Base::SynetPreluLayerForward(src[j], slope[i]);
                    src += size;
                    dst += size;
                }
            }
        }

        void SynetPreluLayerForward(const float * src, const float * slope, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(slope) : Aligned(size)) && Aligned(src) && Aligned(dst))
                SynetPreluLayerForward<true>(src, slope, count, size, dst, trans);
            else
                SynetPreluLayerForward<false>(src, slope, count, size, dst, trans);
        }

        template <bool align> void SynetRestrictRange(const float * src, size_t size, const float * lower, const float * upper, float * dst)
        {
            assert(lower[0] <= upper[0]);
            if (align)
                assert(Aligned(src) && Aligned(dst));
            float min = *lower;
            float max = *upper;
            float32x4_t _min = vdupq_n_f32(min);
            float32x4_t _max = vdupq_n_f32(max);
            size_t sizeF = Simd::AlignLo(size, F);
            size_t sizeQF = Simd::AlignLo(size, QF);
            size_t i = 0;
            for (; i < sizeQF; i += QF)
            {
                Store<align>(dst + i + 0 * F, vminq_f32(vmaxq_f32(_min, Load<align>(src + i + 0 * F)), _max));
                Store<align>(dst + i + 1 * F, vminq_f32(vmaxq_f32(_min, Load<align>(src + i + 1 * F)), _max));
                Store<align>(dst + i + 2 * F, vminq_f32(vmaxq_f32(_min, Load<align>(src + i + 2 * F)), _max));
                Store<align>(dst + i + 3 * F, vminq_f32(vmaxq_f32(_min, Load<align>(src + i + 3 * F)), _max));
            }
            for (; i < sizeF; i += F)
                Store<align>(dst + i, vminq_f32(vmaxq_f32(_min, Load<align>(src + i)), _max));
            for (; i < size; ++i)
                dst[i] = Simd::RestrictRange(src[i], min, max);
        }

        void SynetRestrictRange(const float * src, size_t size, const float * lower, const float * upper, float * dst)
        {
            if (Aligned(src) && Aligned(dst))
                SynetRestrictRange<true>(src, size, lower, upper, dst);
            else
                SynetRestrictRange<false>(src, size, lower, upper, dst);
        }


        template <bool align> SIMD_INLINE void SynetScaleLayerForward(const float * src, const float * scale, const float * bias, float * dst, size_t offset)
        {
            Store<align>(dst + offset, vmlaq_f32(Load<align>(bias + offset), Load<align>(src + offset), Load<align>(scale + offset)));
        }

        template <bool align> SIMD_INLINE void SynetScaleLayerForward(const float * src, const float * scale, float * dst, size_t offset)
        {
            Store<align>(dst + offset, vmulq_f32(Load<align>(src + offset), Load<align>(scale + offset)));
        }

        template <bool align> SIMD_INLINE void SynetScaleLayerForward(const float * src, const float32x4_t & scale, const float32x4_t & bias, float * dst, size_t offset)
        {
            Store<align>(dst + offset, vmlaq_f32(bias, Load<align>(src + offset), scale));
        }

        template <bool align> SIMD_INLINE void SynetScaleLayerForward(const float * src, const float32x4_t & scale, float * dst, size_t offset)
        {
            Store<align>(dst + offset, vmulq_f32(Load<align>(src + offset), scale));
        }

        template <bool align> SIMD_INLINE void SynetScaleLayerForward(const float * src, const float * scale, const float * bias, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (align)
                assert(((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(scale) && Aligned(bias) : Aligned(size)) && Aligned(src) && Aligned(dst));
            if ((trans || size == 1) && count != 1)
            {
                size_t aligned = AlignLo(count, QF);
                size_t partial = AlignLo(count, F);
                if (bias)
                {
                    for (size_t j = 0; j < size; ++j)
                    {
                        size_t i = 0;
                        if (partial)
                        {
                            for (; i < aligned; i += QF)
                            {
                                SynetScaleLayerForward<align>(src, scale, bias, dst, i + F * 0);
                                SynetScaleLayerForward<align>(src, scale, bias, dst, i + F * 1);
                                SynetScaleLayerForward<align>(src, scale, bias, dst, i + F * 2);
                                SynetScaleLayerForward<align>(src, scale, bias, dst, i + F * 3);
                            }
                            for (; i < partial; i += F)
                                SynetScaleLayerForward<align>(src, scale, bias, dst, i);
                        }
                        for (; i < count; ++i)
                            dst[i] = src[i] * scale[i] + bias[i];
                        src += count;
                        dst += count;
                    }
                }
                else
                {
                    for (size_t j = 0; j < size; ++j)
                    {
                        size_t i = 0;
                        if (partial)
                        {
                            for (; i < aligned; i += QF)
                            {
                                SynetScaleLayerForward<align>(src, scale, dst, i + F * 0);
                                SynetScaleLayerForward<align>(src, scale, dst, i + F * 1);
                                SynetScaleLayerForward<align>(src, scale, dst, i + F * 2);
                                SynetScaleLayerForward<align>(src, scale, dst, i + F * 3);
                            }
                            for (; i < partial; i += F)
                                SynetScaleLayerForward<align>(src, scale, dst, i);
                        }
                        for (; i < count; ++i)
                            dst[i] = src[i] * scale[i];
                        src += count;
                        dst += count;
                    }
                }
            }
            else
            {
                size_t aligned = AlignLo(size, QF);
                size_t partial = AlignLo(size, F);
                if (bias)
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        size_t j = 0;
                        if (partial)
                        {
                            float32x4_t _scale = vdupq_n_f32(scale[i]);
                            float32x4_t _bias = vdupq_n_f32(bias[i]);
                            for (; j < aligned; j += QF)
                            {
                                SynetScaleLayerForward<align>(src, _scale, _bias, dst, j + F * 0);
                                SynetScaleLayerForward<align>(src, _scale, _bias, dst, j + F * 1);
                                SynetScaleLayerForward<align>(src, _scale, _bias, dst, j + F * 2);
                                SynetScaleLayerForward<align>(src, _scale, _bias, dst, j + F * 3);
                            }
                            for (; j < partial; j += F)
                                SynetScaleLayerForward<align>(src, _scale, _bias, dst, j);
                        }
                        for (; j < size; ++j)
                            dst[j] = src[j] * scale[i] + bias[i];
                        src += size;
                        dst += size;
                    }
                }
                else
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        size_t j = 0;
                        if (partial)
                        {
                            float32x4_t _scale = vdupq_n_f32(scale[i]);
                            for (; j < aligned; j += QF)
                            {
                                SynetScaleLayerForward<align>(src, _scale, dst, j + F * 0);
                                SynetScaleLayerForward<align>(src, _scale, dst, j + F * 1);
                                SynetScaleLayerForward<align>(src, _scale, dst, j + F * 2);
                                SynetScaleLayerForward<align>(src, _scale, dst, j + F * 3);
                            }
                            for (; j < partial; j += F)
                                SynetScaleLayerForward<align>(src, _scale, dst, j);
                        }
                        for (; j < size; ++j)
                            dst[j] = src[j] * scale[i];
                        src += size;
                        dst += size;
                    }
                }
            }
        }

        void SynetScaleLayerForward(const float * src, const float * scale, const float * bias, size_t count, size_t size, float * dst, SimdBool trans)
        {
            if (((trans || size == 1) && count != 1 ? Aligned(count) && Aligned(scale) && Aligned(bias) : Aligned(size)) && Aligned(src) && Aligned(dst))
                SynetScaleLayerForward<true>(src, scale, bias, count, size, dst, trans);
            else
                SynetScaleLayerForward<false>(src, scale, bias, count, size, dst, trans);
        }

        void SynetSoftmaxLayerForward(const float * src, size_t outer, size_t count, size_t inner, float * dst)
        {
            Exp exp;
            if (inner == 1 && count == 2)
            {
                size_t aligned = Simd::AlignLo(outer, F);
                size_t o = 0;
                for (; o < aligned; o += F)
                {
                    float32x4x2_t s = Load2<false>(src);
                    float32x4_t max = vmaxq_f32(s.val[0], s.val[1]);
                    float32x4_t exp0 = exp.Exponent(vsubq_f32(s.val[0], max));
                    float32x4_t exp1 = exp.Exponent(vsubq_f32(s.val[1], max));
                    float32x4_t sum = vaddq_f32(exp0, exp1);
                    float32x4x2_t d;
                    d.val[0] = Div<1>(exp0, sum);
                    d.val[1] = Div<1>(exp1, sum);
                    Store2<false>(dst, d);
                    src += DF;
                    dst += DF;
                }
                for (; o < outer; ++o)
                {
                    float max = Simd::Max(src[0], src[1]);
                    float exp0 = ::exp(src[0] - max);
                    float exp1 = ::exp(src[1] - max);
                    float sum = exp0 + exp1;
                    dst[0] = exp0 / sum;
                    dst[1] = exp1 / sum;
                    src += 2;
                    dst += 2;
                }
            }
            else
            {
                size_t aligned = Simd::AlignLo(inner, F);
                Array32f tmp(inner * 2);
                const float * s;
                float * max = tmp.data, *sum = tmp.data + inner, *d;
                for (size_t o = 0; o < outer; ++o)
                {
                    memcpy(max, src, inner * sizeof(float));
                    s = src + inner;
                    for (size_t c = 1; c < count; ++c)
                    {
                        size_t i = 0;
                        for (; i < aligned; i += F)
                            Store<false>(max + i, vmaxq_f32(Load<false>(s + i), Load<false>(max + i)));
                        for (; i < inner; ++i)
                            max[i] = Simd::Max(max[i], s[i]);
                        s += inner;
                    }

                    s = src;
                    d = dst;
                    memset(sum, 0, inner * sizeof(float));
                    for (size_t c = 0; c < count; ++c)
                    {
                        size_t i = 0;
                        for (; i < aligned; i += F)
                        {
                            float32x4_t _d = exp.Exponent(vsubq_f32(Load<false>(s + i), Load<false>(max + i)));
                            Store<false>(d + i, _d);
                            Store<false>(sum + i, vaddq_f32(_d, Load<false>(sum + i)));
                        }
                        for (; i < inner; ++i)
                        {
                            d[i] = ::exp(s[i] - max[i]);
                            sum[i] += d[i];
                        }
                        s += inner;
                        d += inner;
                    }

                    d = dst;
                    for (size_t c = 0; c < count; ++c)
                    {
                        size_t i = 0;
                        for (; i < aligned; i += F)
                            Store<false>(d + i, Div<1>(Load<false>(d + i), Load<false>(sum + i)));
                        for (; i < inner; ++i)
                            d[i] /= sum[i];
                        d += inner;
                    }
                    src += count * inner;
                    dst += count * inner;
                }
            }
        }
    }
#endif// SIMD_NEON_ENABLE
}
