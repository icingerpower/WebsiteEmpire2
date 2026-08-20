#ifndef CLIERRORPOLICY_H
#define CLIERRORPOLICY_H

#include "aicli/AbstractCli.h"

#include <QString>

#include <functional>

/**
 * What a caller should do after a failed CLI invocation, decided by a
 * CliErrorCallback (see below).
 */
enum class CliErrorAction {
    Retry,  ///< Re-attempt the exact same job (the human says the issue is fixed).
    Skip,   ///< Give up on this job and move on, same as today's behaviour.
    Abort,  ///< Stop the whole run.
};

/**
 * Decides what to do after a CLI process fails. Receives the CLI that was
 * running, AbstractCli::classifyError()'s verdict for the failure, and the
 * raw stderr text.
 *
 * This lives in WebsiteEmpireLib (not common/aicli) because it is a workflow
 * policy — which action to take — not a property of any specific CLI tool.
 */
using CliErrorCallback =
    std::function<CliErrorAction(const AbstractCli &, CliErrorKind, const QString &)>;

/**
 * Default callback: always Skip, regardless of error kind. Matches the
 * behaviour every caller had before this hook existed, so anything that
 * doesn't explicitly opt in is unaffected.
 */
CliErrorAction defaultCliErrorCallback(const AbstractCli &cli,
                                       CliErrorKind        kind,
                                       const QString       &rawError);

/**
 * Interactive callback for unattended terminal runs (translation, generation
 * launchers). For QuotaExceeded/AuthRequired — the two failure modes a human
 * can actually fix mid-run (wait out the quota, log back in) — prints the
 * error and blocks on stdin: Enter retries the same job, 's' skips it, 'a'
 * aborts the whole run. Any other error kind is Skip, unchanged from today,
 * so unrelated failures don't stall an unattended run waiting for input.
 */
CliErrorAction interactivePauseCliErrorCallback(const AbstractCli &cli,
                                                CliErrorKind        kind,
                                                const QString       &rawError);

#endif // CLIERRORPOLICY_H
