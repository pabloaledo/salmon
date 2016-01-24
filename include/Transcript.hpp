#ifndef TRANSCRIPT
#define TRANSCRIPT

#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include "SalmonStringUtils.hpp"
#include "SalmonUtils.hpp"
#include "SalmonMath.hpp"
#include "SequenceBiasModel.hpp"
#include "FragmentLengthDistribution.hpp"
#include "tbb/atomic.h"

class Transcript {
public:

    Transcript() :
        RefName(nullptr), RefLength(std::numeric_limits<uint32_t>::max()),
        EffectiveLength(-1.0), id(std::numeric_limits<uint32_t>::max()),
        logPerBasePrior_(salmon::math::LOG_0),
        priorMass_(salmon::math::LOG_0),
        mass_(salmon::math::LOG_0), sharedCount_(0.0),
        avgMassBias_(salmon::math::LOG_0),
        active_(false) {
            uniqueCount_.store(0);
            lastUpdate_.store(0);
            lastTimestepUpdated_.store(0);
            cachedEffectiveLength_.store(salmon::math::LOG_0);
        }


    Transcript(size_t idIn, const char* name, uint32_t len, double alpha = 0.05) :
        RefName(name), RefLength(len), EffectiveLength(-1.0), id(idIn),
        logPerBasePrior_(std::log(alpha)),
        priorMass_(std::log(alpha*len)),
        mass_(salmon::math::LOG_0), sharedCount_(0.0),
        avgMassBias_(salmon::math::LOG_0),
        active_(false) {
            uniqueCount_.store(0);
            lastUpdate_.store(0);
            lastTimestepUpdated_.store(0);
            cachedEffectiveLength_.store(std::log(static_cast<double>(RefLength)));
        }

    // We cannot copy; only move
    Transcript(Transcript& other) = delete;
    Transcript& operator=(Transcript& other) = delete;

    Transcript(Transcript&& other) {
        id = other.id;

        RefName = std::move(other.RefName);
        RefLength = other.RefLength;
        EffectiveLength = other.EffectiveLength;

        SAMSequence_ = std::move(other.SAMSequence_);
        Sequence_ = std::move(other.Sequence_);
        GCCount_ = std::move(other.GCCount_);
        gcStep_ = other.gcStep_;
        gcFracLen_ = other.gcFracLen_;
        lastRegularSample_ = other.lastRegularSample_;

        uniqueCount_.store(other.uniqueCount_);
        totalCount_.store(other.totalCount_.load());
        lastTimestepUpdated_.store(other.lastTimestepUpdated_.load());
        sharedCount_.store(other.sharedCount_.load());
        mass_.store(other.mass_.load());
        lastUpdate_.store(other.lastUpdate_.load());
        cachedEffectiveLength_.store(other.cachedEffectiveLength_.load());
        lengthClassIndex_ = other.lengthClassIndex_;
        logPerBasePrior_ = other.logPerBasePrior_;
        priorMass_ = other.priorMass_;
        avgMassBias_.store(other.avgMassBias_.load());
        hasAnchorFragment_.store(other.hasAnchorFragment_.load());
        active_ = other.active_;
    }

    Transcript& operator=(Transcript&& other) {
        id = other.id;

        RefName = std::move(other.RefName);
        RefLength = other.RefLength;
        EffectiveLength = other.EffectiveLength;
        SAMSequence_ = std::move(other.SAMSequence_);
        Sequence_ = std::move(other.Sequence_);
        GCCount_ = std::move(other.GCCount_);
        gcStep_ = other.gcStep_;
        gcFracLen_ = other.gcFracLen_;
        lastRegularSample_ = other.lastRegularSample_;

        uniqueCount_.store(other.uniqueCount_);
        totalCount_.store(other.totalCount_.load());
        lastTimestepUpdated_.store(other.lastTimestepUpdated_.load());
        sharedCount_.store(other.sharedCount_.load());
        mass_.store(other.mass_.load());
        lastUpdate_.store(other.lastUpdate_.load());
        cachedEffectiveLength_.store(other.cachedEffectiveLength_.load());
        lengthClassIndex_ = other.lengthClassIndex_;
        logPerBasePrior_ = other.logPerBasePrior_;
        priorMass_ = other.priorMass_;
        avgMassBias_.store(other.avgMassBias_.load());
        hasAnchorFragment_.store(other.hasAnchorFragment_.load());
        active_ = other.active_;
        return *this;
    }


    inline double sharedCount() { return sharedCount_.load(); }
    inline size_t uniqueCount() { return uniqueCount_.load(); }
    inline size_t totalCount() { return totalCount_.load(); }

    inline void addUniqueCount(size_t newCount) { uniqueCount_ += newCount; }
    inline void addTotalCount(size_t newCount) { totalCount_ += newCount; }

    inline double uniqueUpdateFraction() {
        double ambigCount = static_cast<double>(totalCount_ - uniqueCount_);
        return uniqueCount_ / ambigCount;
    }

    inline char charBaseAt(size_t idx,
                              salmon::stringtools::strand dir = salmon::stringtools::strand::forward) {
        return salmon::stringtools::samCodeToChar[baseAt(idx, dir)];
    }

    inline uint8_t baseAt(size_t idx,
                          salmon::stringtools::strand dir = salmon::stringtools::strand::forward) {
        using salmon::stringtools::strand;
        using salmon::stringtools::encodedRevComp;
        size_t byte = idx >> 1;
        size_t nibble = idx & 0x1;
        uint8_t* sseq = SAMSequence_.get();

        switch(dir) {
        case strand::forward:
            if (nibble) {
                return sseq[byte] & 0x0F;
            } else {
                return ((sseq[byte] & 0xF0) >> 4) & 0x0F;
            }
            break;
        case strand::reverse:
            if (nibble) {
                return encodedRevComp[sseq[byte] & 0x0F];
            } else {
                return encodedRevComp[((sseq[byte] & 0xF0) >> 4) & 0x0F];
            }
            break;
        }

        return std::numeric_limits<uint8_t>::max();
    }

    inline void setSharedCount(double sc) {
        sharedCount_.store(sc);
    }

    inline void addSharedCount(double sc) {
	    salmon::utils::incLoop(sharedCount_, sc);
    }

    inline void setLastTimestepUpdated(uint64_t currentTimestep) {
        uint64_t oldTimestep = lastTimestepUpdated_;
        if (currentTimestep > oldTimestep) {
            lastTimestepUpdated_ = currentTimestep;
        }
    }

    inline void addBias(double bias) {
	salmon::utils::incLoopLog(avgMassBias_, bias);
    }

    inline void addMass(double mass) {
	salmon::utils::incLoopLog(mass_, mass);
    }

    inline void setMass(double mass) {
        mass_.store(mass);
    }

    inline double mass(bool withPrior=true) {
        return (withPrior) ? salmon::math::logAdd(priorMass_, mass_.load()) : mass_.load();
    }

    void setActive() { active_ = true; }
    bool getActive() { return active_; }

    inline double bias() {
        return (totalCount_.load() > 0) ?
                    avgMassBias_ - std::log(totalCount_.load()) :
                    salmon::math::LOG_1;
    }

    /*
    double getAverageSequenceBias(SequenceBiasModel& m) {
        double bias = salmon::math::LOG_0;
        for (int32_t i = 0; i < RefLength; ++i) {
            bias = salmon::math::logAdd(bias, m.biasFactor(*this, i));
        }
        return bias - std::log(RefLength);
    }
    */

    /**
      *  NOTE: Adopted from "est_effective_length" at (https://github.com/adarob/eXpress/blob/master/src/targets.cpp)
      *  originally written by Adam Roberts.
      *
      *
      */
    double computeLogEffectiveLength(
            std::vector<double>& logPMF,
            double logFLDMean,
            size_t minVal,
            size_t maxVal) {

        double effectiveLength = salmon::math::LOG_0;
        double refLen = static_cast<double>(RefLength);
        double logRefLength = std::log(refLen);

        if (logRefLength <= logFLDMean) {
            effectiveLength = logRefLength;
        } else {
            uint32_t mval = maxVal;
            size_t clen = minVal;
            size_t maxLen = std::min(RefLength, mval);
            while (clen <= maxLen) {
                size_t i = clen - minVal;
                effectiveLength = salmon::math::logAdd(
                        effectiveLength,
                        logPMF[i] + std::log(refLen - clen + 1));
                ++clen;
            }
        }
        if (std::exp(effectiveLength) <= 1.0) {
            effectiveLength = salmon::math::LOG_1;
        }

        return effectiveLength;
    }

    /**
     * Return the cached value for the log of the effective length.
     */
    double getCachedLogEffectiveLength() {
        return cachedEffectiveLength_.load();
    }

    void updateEffectiveLength(
            std::vector<double>& logPMF,
            double logFLDMean,
            size_t minVal,
            size_t maxVal) {
        double cel = computeLogEffectiveLength(logPMF, logFLDMean, minVal, maxVal);
        cachedEffectiveLength_.store(cel);
    }

    /**
     * If we should update the effective length, then do it and cache the result.
     * Otherwise, return the cached result.
     */
    /*
    double getLogEffectiveLength(const FragmentLengthDistribution& fragLengthDist,
                                 size_t currObs, size_t burnInObs, bool forceUpdate=false) {
        if (forceUpdate or
            (lastUpdate_ == 0) or
            (currObs - lastUpdate_ >= 250000) or
            (lastUpdate_ < burnInObs and currObs > burnInObs)) {
            // compute new number
            lastUpdate_.store(currObs);
            double cel = computeLogEffectiveLength(fragLengthDist);
            cachedEffectiveLength_.store(cel);
            //priorMass_ = cel + logPerBasePrior_;
            return cachedEffectiveLength_.load();
        } else {
            // return cached number
            return cachedEffectiveLength_.load();
        }
    }
    */

    double perBasePrior() { return std::exp(logPerBasePrior_); }
    inline size_t lastTimestepUpdated() { return lastTimestepUpdated_.load(); }

    void lengthClassIndex(uint32_t ind) { lengthClassIndex_ = ind; }
    uint32_t lengthClassIndex() { return lengthClassIndex_; }

    void setAnchorFragment() {
        hasAnchorFragment_.store(true);
    }

    bool hasAnchorFragment() {
        return hasAnchorFragment_.load();
    }

    // Return the fractional GC content along this transcript
    // in the interval [s,e] (note; this interval is closed on both sides).
    inline int32_t gcFrac(int32_t s, int32_t e) const {
        if (gcStep_ == 1) {
            auto cs = GCCount_[s];
            auto ce = GCCount_[e];
            return std::lrint((100.0 * (ce - cs)) / (e - s + 1));
        } else {
            auto cs = gcCountInterp_(s);
            auto ce = gcCountInterp_(e);
            return std::lrint((100.0 * (ce - cs)) / (e - s + 1));
        }
    }

    // Will *not* delete seq on destruction
    void setSequenceBorrowed(const char* seq, bool needGC=false, uint32_t gcSampFactor=1) {
        Sequence_ = std::unique_ptr<const char, void(*)(const char*)>(
                seq,                 // store seq
                [](const char* p) {} // do nothing deleter
                );
        if (needGC) { computeGCContent_(gcSampFactor); }
    }

    // Will delete seq on destruction
    void setSequenceOwned(const char* seq, bool needGC=false, uint32_t gcSampFactor=1) {
        Sequence_ = std::unique_ptr<const char, void(*)(const char*)>(
                seq,                 // store seq
                [](const char* p) { delete [] p; } // do nothing deleter
                );
        if (needGC) { computeGCContent_(gcSampFactor); }
    }

    // Will *not* delete seq on destruction
    void setSAMSequenceBorrowed(uint8_t* seq, bool needGC=false, uint32_t gcSampFactor=1) {
        SAMSequence_ = std::unique_ptr<uint8_t, void(*)(uint8_t*)>(
                seq,                 // store seq
                [](uint8_t* p) {} // do nothing deleter
                );
        if (needGC) { computeGCContent_(gcSampFactor); }
    }

    // Will delete seq on destruction
    void setSAMSequenceOwned(uint8_t* seq, bool needGC=false,  uint32_t gcSampFactor=1) {
        SAMSequence_ = std::unique_ptr<uint8_t, void(*)(uint8_t*)>(
                seq,                 // store seq
                [](uint8_t* p) { delete [] p; } // do nothing deleter
                );
        if (needGC) { computeGCContent_(gcSampFactor); }
    }

    const char* Sequence() const {
        return Sequence_.get();
    }

    uint8_t* SAMSequence() const {
        return SAMSequence_.get();
    }


    std::string RefName;
    uint32_t RefLength;
    double EffectiveLength;
    uint32_t id;

    double uniqueCounts{0.0};
    double totalCounts{0.0};
    double projectedCounts{0.0};
    double sharedCounts{0.0};

private:
    // NOTE: Is it worth it to check if we have GC here?
    // we should never access these without bias correction.
    inline double gcCount_(int32_t p) {
        return (gcStep_ == 1) ? static_cast<double>(GCCount_[p]) : gcCountInterp_(p);
    }
    inline double gcCount_(int32_t p) const {
        return (gcStep_ == 1) ? static_cast<double>(GCCount_[p]) : gcCountInterp_(p);
    }

    inline double gcCountInterp_(int32_t p) const {
        //std::cerr << "in gcCountInterp\n";
        if (p == RefLength - 1) {
            // If p is the last position, just return the last value
            return static_cast<double>(GCCount_.back());
        }

        // The fractional sampling factor position p would have
        double fracP = static_cast<double>(p) / gcStep_;

        // The largest sampled index for some position <= p
        uint32_t sampInd = std::floor(fracP);

        // The fraction sampling factor for the largest sampled
        // position <= p
        double fracSample = static_cast<double>(sampInd);

        int32_t nextSample{0};
        double fracNextSample{0.0};

        // special case: The last bin may not be evenly spaced.
        if (sampInd >= lastRegularSample_) {
            nextSample = GCCount_.size() - 1;
            fracNextSample = gcFracLen_;
        } else {
            nextSample = sampInd + 1;
            fracNextSample = static_cast<double>(nextSample);
        }
        double lambda = (fracP - fracSample) / (fracNextSample - fracSample);
        return lambda * GCCount_[sampInd] + (1.0 - lambda) * GCCount_[nextSample];
    }

    void computeGCContentSampled_(uint32_t step) {
        gcStep_ = step;
        const char* seq = Sequence_.get();
        size_t nsamp = std::ceil(static_cast<double>(RefLength) / step);
        GCCount_.reserve(nsamp + 2);

        size_t lastSamp{0};
        size_t totGC{0};
        for (size_t i = 0; i < RefLength; ++i) {
            auto c = std::toupper(seq[i]);
            if (c == 'G' or c == 'C') {
                totGC++;
            }
            if (i % step == 0) {
                GCCount_.push_back(totGC);
                lastSamp = i;
            }
        }

        if (lastSamp < RefLength - 1) {
            GCCount_.push_back(totGC);
        }

        gcFracLen_ = static_cast<double>(RefLength - 1) / gcStep_;
        lastRegularSample_ = std::ceil(gcFracLen_);
    }

    void computeGCContent_(uint32_t gcSampFactor) {
        const char* seq = Sequence_.get();
        GCCount_.clear();
        if (gcSampFactor == 1) {
            GCCount_.resize(RefLength, 0);
            size_t totGC{0};
            for (size_t i = 0; i < RefLength; ++i) {
                auto c = std::toupper(seq[i]);
                if (c == 'G' or c == 'C') {
                    totGC++;
                }
                GCCount_[i] = totGC;
            }
        } else {
            computeGCContentSampled_(gcSampFactor);
        }
    }

    std::unique_ptr<uint8_t, void(*)(uint8_t*)> SAMSequence_ =
        std::unique_ptr<uint8_t, void(*)(uint8_t*)> (nullptr, [](uint8_t*){});

    std::unique_ptr<const char, void(*)(const char*)> Sequence_ =
        std::unique_ptr<const char, void(*)(const char*)> (nullptr, [](const char*){});

    std::atomic<size_t> uniqueCount_;
    std::atomic<size_t> totalCount_;
    // The most recent timestep at which this transcript's mass was updated.
    std::atomic<size_t> lastTimestepUpdated_;
    double priorMass_;
    tbb::atomic<double> mass_;
    tbb::atomic<double> sharedCount_;
    tbb::atomic<double> cachedEffectiveLength_;
    tbb::atomic<size_t> lastUpdate_;
    tbb::atomic<double> avgMassBias_;
    uint32_t lengthClassIndex_;
    double logPerBasePrior_;
    // In a paired-end protocol, a transcript has
    // an "anchor" fragment if it has a proper
    // pair of reads mapping to it.
    std::atomic<bool> hasAnchorFragment_{false};
    bool active_;

    uint32_t gcStep_{1};
    double gcFracLen_{0.0};
    uint32_t lastRegularSample_{0};
    std::vector<uint32_t> GCCount_;
};

#endif //TRANSCRIPT
