#pragma once

#include "Model/PhraseModel.h"

#include <vector>

namespace mixdesk::engine
{
class PhraseAnalyzer
{
public:
    [[nodiscard]] std::vector<model::PhraseBlock> analyze(const model::BeatGrid& beatGrid,
        const std::vector<model::StemWaveform>& stemWaveforms,
        int barsPerPhrase = 8) const;
};
} // namespace mixdesk::engine
