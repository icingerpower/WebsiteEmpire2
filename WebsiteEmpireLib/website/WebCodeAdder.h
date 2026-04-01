#ifndef WEBCODEADDER_H
#define WEBCODEADDER_H

#include <QSet>
#include <QString>
#include <QStringView>

/**
 * Interface for objects that contribute HTML, CSS and JS fragments during page generation.
 *
 * Implementations append their content to the three output strings.
 * origContent is a view into the raw source text (e.g. a shortcode block); no copy is made.
 */
class WebCodeAdder
{
public:
    virtual ~WebCodeAdder() = default;

    /**
     * Append generated HTML, CSS and JS fragments to the output strings.
     *
     * cssDoneIds / jsDoneIds track which CSS / JS blocks have already been
     * emitted into the page so that identical blocks are not duplicated.
     * Implementations must check these sets before appending CSS/JS and
     * insert the block's id into the set when they write for the first time.
     */
    virtual void addCode(QStringView     origContent,
                         QString        &html,
                         QString        &css,
                         QString        &js,
                         QSet<QString>  &cssDoneIds,
                         QSet<QString>  &jsDoneIds) const = 0;
};

#endif // WEBCODEADDER_H
