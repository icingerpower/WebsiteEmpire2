#ifndef ABSTRACTLEGALPAGEDEF_H
#define ABSTRACTLEGALPAGEDEF_H

#include <QList>
#include <QString>

/**
 * Base class describing a required legal page.
 *
 * Each concrete subclass declares one mandatory legal page that the website
 * must have (e.g. Privacy Policy, Terms of Service).  The registry lets the
 * system determine whether all required pages exist and which are still missing.
 *
 * Self-registration
 * -----------------
 * Place DECLARE_LEGAL_PAGE(ClassName) at file scope in the .cpp.
 * The instance is then accessible via AbstractLegalPageDef::allDefs().
 * Adding a new subclass + macro immediately makes the "Generate Legal Pages"
 * button non-grey until the new page is created.
 *
 * Internal page_data stamp
 * ------------------------
 * When generateLegalPages() creates or refreshes a page, it stores
 *   PAGE_DATA_KEY_DEF_ID → getId()
 * in the page's page_data.  This stamp survives permalink edits and lets the
 * system reliably detect which defs are already covered without relying on
 * the permalink remaining unchanged.
 *
 * The double-underscore prefix on the key marks it as an internal system key:
 * it is excluded from AI translation prompts and must never be shown in the editor.
 */
class AbstractLegalPageDef
{
public:
    // Key stored in page_data to link a page to its AbstractLegalPageDef.
    static constexpr const char *PAGE_DATA_KEY_DEF_ID = "__legal_def_id";

    /**
     * All legal settings drawn from WebsiteSettingsTable, bundled for
     * passing to generateTextContent().  Optional fields may be empty.
     */
    struct LegalInfo {
        QString companyName;
        QString companyAddress;
        QString registrationNo;
        QString contactEmail;
        QString vatNo;       ///< optional
        QString dpoName;     ///< optional (GDPR)
        QString dpoEmail;    ///< optional (GDPR)
        QString websiteName;
    };

    virtual ~AbstractLegalPageDef() = default;

    /** Stable identifier — never change once pages have been created. */
    virtual QString getId() const = 0;

    /** Human-readable name shown in the generation dialog. */
    virtual QString getDisplayName() const = 0;

    /** Suggested permalink used when creating the page for the first time. */
    virtual QString getDefaultPermalink() const = 0;

    /**
     * Returns the initial English text content for this legal page, pre-filled
     * with the values from info.
     *
     * The returned string is stored in the "1_text" page_data key of the text
     * bloc (PageBlocText::KEY_TEXT, bloc index 1).  Blank lines separate
     * paragraphs; block-level HTML (e.g. <h2>) may be embedded as standalone
     * paragraphs.
     *
     * This is called only when a new page is created; existing pages are never
     * overwritten by this method.
     */
    virtual QString generateTextContent(const LegalInfo &info) const = 0;

    // -------------------------------------------------------------------------
    // Registry
    // -------------------------------------------------------------------------

    /** Returns all registered definitions in registration order. */
    static const QList<AbstractLegalPageDef *> &allDefs();

    /**
     * Called by DECLARE_LEGAL_PAGE at static-initialisation time.
     * Do not call directly.
     */
    static void registerDef(AbstractLegalPageDef *def);
};

/**
 * Place at file scope in the .cpp of each concrete AbstractLegalPageDef subclass.
 */
#define DECLARE_LEGAL_PAGE(ClassName)                                               \
    namespace {                                                                     \
        static ClassName s_##ClassName##_instance;                                  \
        static const bool s_##ClassName##_registered =                             \
            (AbstractLegalPageDef::registerDef(&s_##ClassName##_instance), true);  \
    }

#endif // ABSTRACTLEGALPAGEDEF_H
