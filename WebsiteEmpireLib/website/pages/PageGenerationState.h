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
 *                     │                 │               ▲
 *                     │                 └─(SocialMedia)──► SocialComplete
 *                     └── (no [IMGFIX] shortcodes) ─────┘
 *
 * Complete      = first pass done (article + SVG if needed).  No social-media
 *                 variant images.
 * SocialComplete = first pass done AND social-media second pass ran and saved
 *                 at least one WebP variant.
 *
 * LauncherGeneration reads this state on startup and resumes from the correct
 * step, so a crash never discards already-completed work.
 *
 * The enum is also reused for per-language translation image state, stored in
 * page_translation_image_states.  Only Pending (0) and Complete (3) are used
 * for translation: intermediate states are not meaningful there.
 */
enum class PageGenerationState : int {
    Pending         = 0,  ///< nothing generated yet
    ContentReady    = 1,  ///< article text + metadata saved; SVG still needed
    MainImageReady  = 2,  ///< source SVG written; social-media second pass pending
    Complete        = 3,  ///< first pass done (article + SVG); no social images
    SocialComplete  = 4,  ///< Complete + social-media variant images saved
};

#endif // PAGEGENERATIONSTATE_H
