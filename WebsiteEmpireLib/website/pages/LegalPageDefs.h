#ifndef LEGALPAGEDEFS_H
#define LEGALPAGEDEFS_H

#include "AbstractLegalPageDef.h"

/**
 * Standard set of required legal pages.
 *
 * To add a new mandatory legal page (e.g. if legislation changes):
 *   1. Declare a new subclass here.
 *   2. Implement getId() / getDisplayName() / getDefaultPermalink() in the .cpp.
 *   3. Add DECLARE_LEGAL_PAGE(ClassName) at file scope in the .cpp.
 *
 * The "Generate Legal Pages" button in PanePages turns non-grey automatically
 * as soon as a new def is registered and the corresponding page does not yet
 * exist in the repository.
 */

class LegalPageDefPrivacyPolicy : public AbstractLegalPageDef
{
public:
    QString getId()               const override;
    QString getDisplayName()      const override;
    QString getDefaultPermalink() const override;
    QString generateTextContent(const LegalInfo &info) const override;
};

class LegalPageDefTermsOfService : public AbstractLegalPageDef
{
public:
    QString getId()               const override;
    QString getDisplayName()      const override;
    QString getDefaultPermalink() const override;
    QString generateTextContent(const LegalInfo &info) const override;
};

class LegalPageDefCookiePolicy : public AbstractLegalPageDef
{
public:
    QString getId()               const override;
    QString getDisplayName()      const override;
    QString getDefaultPermalink() const override;
    QString generateTextContent(const LegalInfo &info) const override;
};

class LegalPageDefLegalNotice : public AbstractLegalPageDef
{
public:
    QString getId()               const override;
    QString getDisplayName()      const override;
    QString getDefaultPermalink() const override;
    QString generateTextContent(const LegalInfo &info) const override;
};

class LegalPageDefContactUs : public AbstractLegalPageDef
{
public:
    QString getId()               const override;
    QString getDisplayName()      const override;
    QString getDefaultPermalink() const override;
    QString generateTextContent(const LegalInfo &info) const override;
};

class LegalPageDefAboutUs : public AbstractLegalPageDef
{
public:
    QString getId()               const override;
    QString getDisplayName()      const override;
    QString getDefaultPermalink() const override;
    QString generateTextContent(const LegalInfo &info) const override;
};

#endif // LEGALPAGEDEFS_H
