#ifndef ARTICLECONTENTVALIDATOR_H
#define ARTICLECONTENTVALIDATOR_H

#include <QString>

/**
 * Shared validation for AI-generated/updated article body text.
 *
 * Used by both LauncherGeneration and LauncherUpdate before a page is saved,
 * so the two launchers cannot silently drift apart and each rule is
 * independently unit-testable (the previous duplicated static functions in
 * each launcher's .cpp file could not be unit tested directly).
 *
 * isValid() checks, in order:
 *   1. After stripping any inline <svg>...</svg> block, the text must start
 *      with a well-formed [TITLE level="1"]...[/TITLE] shortcode.
 *   2. The cleaned text must be at least 2000 characters (rejects truncated
 *      or filler-only responses).
 *   3. No raw Markdown artifacts — see hasMarkdownArtifact().
 *   4. No malformed shortcode arguments — see
 *      AbstractShortCode::validateAllInText().
 */
class ArticleContentValidator
{
public:
    static bool isValid(const QString &text);

    /**
     * Returns true when text contains a Markdown table (a row matching the
     * "| --- | --- |" header-separator convention) or a literal "[NEWLINE]"
     * placeholder token. Both are tell-tale signs that the AI wrote free-form
     * Markdown instead of using the shortcode-only format this system
     * renders — the pipe characters and literal token text leak straight
     * into the rendered HTML unescaped (see the Healybio osteoarthritis
     * article regression: Antigravity wrote a Markdown summary table with
     * "[NEWLINE]" instead of real newlines inside table cells, and it was
     * published as-is because no known-shortcode syntax was involved).
     */
    static bool hasMarkdownArtifact(const QString &text);
};

#endif // ARTICLECONTENTVALIDATOR_H
