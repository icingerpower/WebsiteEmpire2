#ifndef PAGEGENERATIONSTATE_H
#define PAGEGENERATIONSTATE_H

/**
 * Tracks how far the AI generation pipeline has progressed for a source page.
 *
 * Stored as INTEGER in pages.generation_state.  Rows that existed before this
 * column was added are assigned Complete (3) when generated_at IS NOT NULL,
 * or Pending (0) otherwise — see PageDb::createSchema().
 *
 * Transition diagram:
 *
 *   Pending ──► ContentReady ──► MainImageReady ──► Complete
 *                     │                                 ▲
 *                     └── (no [IMGFIX] shortcodes) ─────┘
 *
 * LauncherGeneration reads this state on startup and resumes from the correct
 * step, so a crash never discards already-completed work.
 *
 * PageTranslator refuses to translate any page whose state is not Complete.
 *
 * The same enum is reused for per-language translation image state, stored in
 * page_translation_image_states.  Only Pending (0) and Complete (3) are used
 * for translation: intermediate states are not needed because the translation
 * pipeline commits SVG + WebP atomically for each variant.
 */
enum class PageGenerationState : int {
    Pending        = 0,  ///< nothing generated yet
    ContentReady   = 1,  ///< pass 1 done: article text + metadata saved
    MainImageReady = 2,  ///< [IMGFIX] source SVG(s) written to images.db
    Complete       = 3,  ///< pass 2 done: social SVGs + WebPs written
};

#endif // PAGEGENERATIONSTATE_H
