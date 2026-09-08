#ifndef RASTERIMAGESTATUS_H
#define RASTERIMAGESTATUS_H

/**
 * Per-image outcome for a raster (non-SVG) [IMGFIX] reference, tracked in the
 * page_raster_images table (see PageDb).
 *
 * Pending     = not yet generated, or a previous attempt failed and more
 *               attempts remain (see LauncherGeneration's kRasterMaxAttempts).
 * Success     = the image was generated, reviewed OK, and stored in images.db.
 * FailedFinal = every allowed attempt was exhausted without a passing review.
 *               The page's generation loop stops retrying this image, but a
 *               page with any FailedFinal (or Pending) raster image is never
 *               published — see PageGenerator's publish-time gate — until a
 *               human resets it back to Pending for another try.
 *
 * Pending and FailedFinal are both "not ready to publish"; the distinction
 * exists so the automatic retry loop (LauncherGeneration) knows when to stop
 * spending CLI calls on an image, separately from whether the page as a whole
 * is safe to serve.
 */
enum class RasterImageStatus : int {
    Pending     = 0,
    Success     = 1,
    FailedFinal = 2,
};

#endif // RASTERIMAGESTATUS_H
