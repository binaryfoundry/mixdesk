#include "DemoState.h"

namespace mixdesk::model
{
WorkspaceState createDemoWorkspaceState()
{
    // TODO(beatgrid/phrase analysis): replace this handcrafted state with analyzed track metadata.
    WorkspaceState state;
    state.bpm = 126.0;
    state.barsPerPhrase = 8;
    state.currentBarPosition = 30.5;

    DeckTimeline deckA;
    deckA.id = DeckId::A;
    deckA.role = DeckRole::Lead;
    deckA.launchOffsetBars = 0;
    deckA.volume = 0.92f;
    deckA.lowCutEnabled = false;
    deckA.blocks = {
        { PhraseType::Groove,    0, 16, 0.72f, true,  false, true,  true  },
        { PhraseType::Breakdown, 16, 8, 0.34f, false, true,  false, true  },
        { PhraseType::Build,     24, 8, 0.66f, false, true,  true,  true  },
        { PhraseType::Drop,      32, 16, 0.96f, true,  false, true,  true  },
    };

    DeckTimeline deckB;
    deckB.id = DeckId::B;
    deckB.role = DeckRole::Incoming;
    deckB.launchOffsetBars = 0;
    deckB.volume = 1.0f;
    deckB.lowCutEnabled = false;

    DeckTimeline deckC;
    deckC.id = DeckId::C;
    deckC.role = DeckRole::RhythmLayer;
    deckC.launchOffsetBars = 0;
    deckC.volume = 1.0f;
    deckC.lowCutEnabled = false;

    state.decks = { deckA, deckB, deckC };
    return state;
}
} // namespace mixdesk::model
