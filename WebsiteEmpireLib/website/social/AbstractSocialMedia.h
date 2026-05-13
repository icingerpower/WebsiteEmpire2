#ifndef ABSTRACTSOCIALMEDIA_H
#define ABSTRACTSOCIALMEDIA_H

#include <QList>
#include <QSize>
#include <QString>

/**
 * Base class for a social media platform descriptor.
 *
 * Each concrete subclass (SocialMediaOpenGraph, SocialMediaTwitter, etc.)
 * declares which image size variant it requires and produces the appropriate
 * <meta> tags for the HTML <head>.
 *
 * Self-registration
 * -----------------
 * Use the DECLARE_SOCIAL_MEDIA(ClassName) macro in the .cpp to register the
 * platform into the global all() list.  Order in the list matches declaration
 * order across translation units (undefined; do not rely on it).
 *
 * Image sizes
 * -----------
 * Four canonical sizes cover all major platforms:
 *   Landscape 1200×630  — Facebook, LinkedIn, WhatsApp, Telegram, Slack, Discord
 *   Wide      1200×675  — Google Discover / schema.org (16:9)
 *   Square    1200×1200 — Twitter summary card, Instagram
 *   Portrait  1000×1500 — Pinterest (2:3 preferred for pins)
 *
 * File naming
 * -----------
 * SVG and WebP variants are named:
 *   {article-slug}-og.svg / .webp       (Landscape)
 *   {article-slug}-wide.svg / .webp     (Wide)
 *   {article-slug}-square.svg / .webp   (Square)
 *   {article-slug}-portrait.svg / .webp (Portrait)
 *
 * Use svgSuffix() / webpSuffix() to derive the correct filename suffix for a
 * given size rather than constructing strings manually.
 */
class AbstractSocialMedia
{
public:
    enum class ImageSize { Landscape, Wide, Square, Portrait };

    virtual ~AbstractSocialMedia() = default;

    /** Stable machine identifier (e.g. "opengraph", "twitter"). */
    virtual QString getId() const = 0;

    /** Human-readable platform name (translated). */
    virtual QString getName() const = 0;

    /** Which image variant this platform requires. */
    virtual ImageSize requiredImageSize() const = 0;

    /**
     * Returns the <meta> HTML for the image tag(s) for this platform.
     * imageUrl is the absolute URL of the WebP for requiredImageSize().
     */
    virtual QString imageMetaTagHtml(const QString &imageUrl) const = 0;

    /**
     * Returns the <meta> HTML for the page title for this platform.
     * Returns an empty string if the platform does not use a title tag.
     */
    virtual QString titleMetaTagHtml(const QString &title) const = 0;

    /**
     * Returns the <meta> HTML for the page description for this platform.
     * Returns an empty string if the platform does not use a description tag.
     */
    virtual QString descMetaTagHtml(const QString &desc) const = 0;

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    /** Pixel dimensions for the given size. */
    static QSize imageSizeDimensions(ImageSize size);

    /**
     * Filename suffix (including the leading '-') for an SVG variant.
     * Example: svgSuffix(Landscape) == "-og.svg"
     */
    static QString svgSuffix(ImageSize size);

    /**
     * Filename suffix (including the leading '-') for a WebP variant.
     * Example: webpSuffix(Landscape) == "-og.webp"
     */
    static QString webpSuffix(ImageSize size);

    // -------------------------------------------------------------------------
    // Registry
    // -------------------------------------------------------------------------

    /** Returns all registered platforms in registration order. */
    static const QList<AbstractSocialMedia *> &all();

    /** Called by DECLARE_SOCIAL_MEDIA to insert into all(). Not for direct use. */
    static void registerPlatform(AbstractSocialMedia *platform);
};

/**
 * Self-registers a concrete AbstractSocialMedia subclass into all().
 * Place exactly once in the .cpp of each platform class.
 */
#define DECLARE_SOCIAL_MEDIA(ClassName)                                     \
    namespace {                                                              \
    struct ClassName##_Registrar {                                           \
        ClassName##_Registrar() {                                            \
            AbstractSocialMedia::registerPlatform(new ClassName);            \
        }                                                                    \
    };                                                                       \
    static ClassName##_Registrar s_##ClassName##_registrar;                 \
    }

#endif // ABSTRACTSOCIALMEDIA_H
