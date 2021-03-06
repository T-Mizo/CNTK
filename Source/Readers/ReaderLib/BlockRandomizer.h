//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//

#pragma once

#include <vector>

#include "SequenceEnumerator.h"
#include "DataDeserializer.h"
#include "ChunkRandomizer.h"
#include "SequenceRandomizer.h"
#include <future>

namespace Microsoft { namespace MSR { namespace CNTK {

// A randomizer that firstly randomizes chunks and then sequences inside a rolling window of chunks.
// Uses ChunkRandomizer to randomize chunk descriptions and SequenceRandomizer to randomize sequence descriptions inside a window of chunks.
// It requires only a window of sequence descriptions and corresponding chunk data.
// The code is based on the old block randomizer and it preserves the same behavior to pass all available tests.
// The high-level algorithm is:
//     When next sequences are requested (limited by the sampleCount), the following steps are performed:
//         1) if a new sweep is entered, randomize chunk descriptions using ChunkRandomizer, also precalculate randomization windows for all
//            chunk descriptions
//         2) if a new chunk is entered, using SequenceRandomizer identify a window of chunks and requested their sequence descriptions from deserializer.
//         3) randomize sequence descriptions inside the window
//         4) return sequence descriptions not exceeding sampleCount/minibatch limit
//         5) decimate sequence descriptions based on the worker rank
//         6) request chunks of data based on decimated sequences and return sequence data
//
// This class is responsible for decimation and loading the data chunks in to memory.
// Actual randomization happens in ChunkRandomizer and SequenceRandomizer.
// TODO: The behavior can be simplified by only randomizing sequences forward.
class BlockRandomizer : public SequenceEnumerator
{
public:
    BlockRandomizer(
        int verbosity,
        size_t randomizationRangeInSamples,
        IDataDeserializerPtr deserializer,
        bool shouldPrefetch,
        bool useLegacyRandomization = false,
        bool multithreadedGetNextSequences = false);

    // Starts a new epoch.
    virtual void StartEpoch(const EpochConfiguration& config) override;

    // Gets next sequences.
    virtual Sequences GetNextSequences(size_t sampleCount) override;

    // Gets stream descriptions.
    virtual std::vector<StreamDescriptionPtr> GetStreamDescriptions() const override
    {
        return m_deserializer->GetStreamDescriptions();
    }

    // Returns current position in the global timeline. The returned value is in samples.
    size_t GetCurrentSamplePosition() override;

    ~BlockRandomizer()
    {
        if (m_prefetch.valid())
        {
            m_prefetch.wait();
        }
    }

    void SetCurrentSamplePosition(size_t currentSamplePosition) override;

    void SetConfiguration(const ReaderConfiguration& config) override;

private:
    // Load data for chunks if needed.
    void LoadDataChunks(const ClosedOpenChunkInterval& windowRange);

    // Get next sequence descriptions that do not exceed sample count.
    // Returns true if epoch end is reached.
    bool GetNextSequenceDescriptions(size_t sampleCount, std::vector<RandomizedSequenceDescription>& result, ClosedOpenChunkInterval& windowRange);

    // Decimates sequence descriptions and loads chunks of data.
    void Decimate(const std::vector<RandomizedSequenceDescription>& all, std::vector<RandomizedSequenceDescription>& decimated);

    // Prepares a new sweep if needed.
    void PrepareNewSweepIfNeeded(size_t samplePosition);

    // Performs io prefetch of the specified chunk if needed.
    void Prefetch(ChunkIdType chunkId);

    // Returns next candidate for the prefetch in the given range.
    ChunkIdType GetChunkToPrefetch(const ClosedOpenChunkInterval& windowRange);

    // Global sample position on the timeline.
    size_t m_globalSamplePosition;

    // Global start position;
    size_t m_epochStartPosition;

    // Configuration of the epoch.
    EpochConfiguration m_config;

    // Epoch size.
    size_t m_epochSize;

    // Current sweep.
    size_t m_sweep;

    // Global position of the current sweep in samples.
    size_t m_sweepStartInSamples;

    // Total number of samples in a sweep.
    size_t m_sweepTotalNumberOfSamples;

    IDataDeserializerPtr m_deserializer;

    // Chunk randomizer.
    ChunkRandomizerPtr m_chunkRandomizer;

    // Sequence randomizer.
    SequenceRandomizerPtr m_sequenceRandomizer;

    // Exposed streams.
    std::vector<StreamDescriptionPtr> m_streams;

    // A map of data chunks from original chunk id into chunk.
    std::map<size_t, ChunkPtr> m_chunks;

    // Whether to get sequences using multiple thread.
    bool m_multithreadedGetNextSequences;

    // General configuration
    // TODO generalize those for ReaderLib / Reader / CNTK
    enum VerbosityLevel
    {
        Warning = 0,
        Notification = 1,
        Information = 2,
        Debug = 3,
    };

    int m_verbosity;

    // Prefetch future.
    std::future<ChunkPtr> m_prefetch;
    // Whether to have async or deferred prefetch.
    launch m_launchType;
    // Prefetched original chunk id.
    ChunkIdType m_prefetchedChunk;

    // Current loaded chunks.
    ClosedOpenChunkInterval m_currentWindowRange;
};

}}}
