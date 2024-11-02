#include "../Common/clap_eventsequence.h"
#include "tests/choc_UnitTest.h"
#include <random>

// does just some basic sanity checking for equality
inline void compareSequences(choc::test::TestProgress &progress, const ClapEventSequence &seqA,
                             const ClapEventSequence &seqB)
{
    CHOC_EXPECT_TRUE(seqA.getNumEvents() == seqB.getNumEvents());
    for (size_t i = 0; i < seqA.m_evlist.size(); ++i)
    {
        CHOC_EXPECT_TRUE(seqA.m_evlist[i].timestamp == seqB.m_evlist[i].timestamp)
        if (seqA.m_evlist[i].event.header.type == CLAP_EVENT_NOTE_ON)
        {
            auto nev0 = (clap_event_note *)&seqA.m_evlist[i].event;
            auto nev1 = (clap_event_note *)&seqB.m_evlist[i].event;
            CHOC_EXPECT_TRUE(nev0->key == nev1->key);
            CHOC_EXPECT_TRUE(nev0->velocity == nev1->velocity);
        }
    }
}

void test_clap_sequence(choc::test::TestProgress &progress)
{

    CHOC_CATEGORY(ClapEventSequence)
    {
        CHOC_TEST(Basic)
        ClapEventSequence seq;
        CHOC_EXPECT_TRUE(seq.getNumEvents() == 0);

        seq.addNoteOn(0.0, 0, 0, 60, 0.9, -1);
        CHOC_EXPECT_TRUE(seq.getNumEvents() == 1);
        seq.clearEvents();
        CHOC_EXPECT_TRUE(seq.getNumEvents() == 0);
        seq.addNote(0.0, 1.0, 0, 0, 60, -1, 0.9, 0.0);
        CHOC_EXPECT_TRUE(seq.getNumEvents() == 2);
        seq.clearEvents();
        seq.addNote(0.0, 1.0, 0, 0, 60, -1, 0.9, 0.531);
        CHOC_EXPECT_TRUE(seq.getNumEvents() == 3);
        seq.clearEvents();
        seq.addNoteF(0.0, 1.0, 0, 0, 60.0, -1, 0.12);
        CHOC_EXPECT_TRUE(seq.getNumEvents() == 2);
        seq.clearEvents();
        seq.addNoteF(0.0, 1.0, 0, 0, 60.19, -1, 0.12);
        CHOC_EXPECT_TRUE(seq.getNumEvents() == 3);
    }
    {
        CHOC_TEST(Serialization)
        ClapEventSequence seq;
        std::mt19937 rng(9000);
        std::uniform_int_distribution<int> keydist{48, 72};
        std::uniform_real_distribution<double> velodist(0.1, 1.0);
        for (size_t i = 0; i < 100; ++i)
        {
            seq.addNote(i * 1.0, 1.0, 0, 0, keydist(rng), -1, velodist(rng), 0.0);
        }
        {
            auto tree = seq.toValueTree("Test");
            CHOC_EXPECT_TRUE(tree.isObject());
            auto deserialized = ClapEventSequence::fromValueTree(tree);
            compareSequences(progress, seq, deserialized);
        }
        {
            auto json = seq.toJSON();
            auto deserialized = ClapEventSequence::fromJSON(json);
            compareSequences(progress, seq, deserialized);
        }
    }
}

void run_tests()
{
    choc::test::TestProgress progress;
    test_clap_sequence(progress);
}
