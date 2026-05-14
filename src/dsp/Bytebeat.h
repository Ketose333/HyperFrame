#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace hyperframe::dsp {

bool evaluateBytebeatExpression(std::string_view expression, std::uint32_t t, std::int64_t& result);

bool renderBytebeatTicks(std::string_view expression,
                         std::uint32_t startTick,
                         std::size_t tickCount,
                         std::vector<float>& samples);

bool renderBytebeat(std::string_view expression,
                    double sampleRate,
                    double startSeconds,
                    double durationSeconds,
                    std::vector<float>& samples);

} // namespace hyperframe::dsp
