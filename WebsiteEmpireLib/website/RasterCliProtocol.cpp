#include "RasterCliProtocol.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

QString RasterCliProtocol::decodeAntigravity(const QByteArray &output, bool requireImage,
                                            const QString &expectedImagePath)
{
    bool imageDone = false;
    bool inspectionDone = false;
    bool resultSeen = false;
    QString response;
    QString toolError;
    QString priorityError;
    const auto lines = output.split('\n');
    for (const auto &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return QStringLiteral("__ERROR__: IMAGE_TOOL_UNAVAILABLE: invalid Antigravity event stream");
        }
        const auto event = document.object();
        const auto step = event.value(QStringLiteral("step_update")).toObject();
        const auto info = step.value(QStringLiteral("tool_info")).toObject();
        const auto error = info.value(QStringLiteral("error")).toObject();
        if (!requireImage && step.value(QStringLiteral("step_type")).toString() == QStringLiteral("tool")
            && step.value(QStringLiteral("tool_name")).toString() != QStringLiteral("view_file")) {
            toolError = QStringLiteral("review used tools other than view_file");
        }
        const QString message = error.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            toolError = message;
            if (message.contains(QStringLiteral("quota"), Qt::CaseInsensitive)
                || message.contains(QStringLiteral("RESOURCE_EXHAUSTED"))
                || message.contains(QStringLiteral("Authentication required"), Qt::CaseInsensitive)) {
                priorityError = message;
            }
        }
        const QString toolName = step.value(QStringLiteral("tool_name")).toString();
        const QString stepType = step.value(QStringLiteral("step_type")).toString();
        if (toolName == QStringLiteral("generate_image")) {
            if (step.value(QStringLiteral("state")).toString() == QStringLiteral("DONE")
                && error.isEmpty()) {
                imageDone = true;
            } else if (step.value(QStringLiteral("state")).toString() == QStringLiteral("ERROR")
                       && message.isEmpty()) {
                toolError = QStringLiteral("native generate_image failed without an error message");
            }
        }
        if (toolName == QStringLiteral("invoke_subagent") || stepType == QStringLiteral("subagent")) {
            const auto subagentInfo = step.value(QStringLiteral("subagent_info")).toObject();
            const auto subagents = subagentInfo.value(QStringLiteral("subagents")).toArray();
            for (const auto &subVal : subagents) {
                const auto subObj = subVal.toObject();
                if (subObj.value(QStringLiteral("type_name")).toString() == QStringLiteral("image-generator")) {
                    const QString logUriStr = subObj.value(QStringLiteral("log_uri")).toString();
                    const QString localLogPath = QUrl(logUriStr).toLocalFile();
                    if (!localLogPath.isEmpty() && QFile::exists(localLogPath)) {
                        QFile logFile(localLogPath);
                        if (logFile.open(QIODevice::ReadOnly)) {
                            bool subagentGenSuccess = false;
                            while (!logFile.atEnd()) {
                                const QByteArray subLine = logFile.readLine().trimmed();
                                if (subLine.isEmpty()) {
                                    continue;
                                }
                                QJsonParseError subErr;
                                const auto subDoc = QJsonDocument::fromJson(subLine, &subErr);
                                if (!subDoc.isObject()) {
                                    continue;
                                }
                                const auto subEvent = subDoc.object();
                                const QString subError = subEvent.value(QStringLiteral("error")).toString();
                                if (!subError.isEmpty()) {
                                    if (subError.contains(QStringLiteral("quota"), Qt::CaseInsensitive)
                                        || subError.contains(QStringLiteral("RESOURCE_EXHAUSTED"))
                                        || subError.contains(QStringLiteral("Authentication required"), Qt::CaseInsensitive)) {
                                        priorityError = subError;
                                    } else if (toolError.isEmpty()) {
                                        toolError = subError;
                                    }
                                }
                                const auto mediaArr = subEvent.value(QStringLiteral("media")).toArray();
                                if (!mediaArr.isEmpty()) {
                                    subagentGenSuccess = true;
                                }
                                const QString content = subEvent.value(QStringLiteral("content")).toString();
                                if (content.contains(QStringLiteral("Generated image is saved"), Qt::CaseInsensitive)
                                    || content.contains(QStringLiteral("Final image generated"), Qt::CaseInsensitive)) {
                                    subagentGenSuccess = true;
                                }
                            }
                            if (subagentGenSuccess) {
                                imageDone = true;
                            }
                        }
                    } else if (step.value(QStringLiteral("state")).toString() == QStringLiteral("DONE")
                               && error.isEmpty()) {
                        imageDone = true;
                    }
                }
            }
        }
        if (step.value(QStringLiteral("tool_name")).toString() == QStringLiteral("view_file")
            && step.value(QStringLiteral("state")).toString() == QStringLiteral("DONE")
            && error.isEmpty()) {
            const auto parameters = info.value(QStringLiteral("parameters")).toObject();
            if (expectedImagePath.isEmpty()
                || parameters.value(QStringLiteral("AbsolutePath")).toString() == expectedImagePath) {
                inspectionDone = true;
            } else if (!requireImage) {
                toolError = QStringLiteral("review inspected a file other than the candidate image");
            }
        }
        if (event.value(QStringLiteral("event")).toString() == QStringLiteral("result")) {
            resultSeen = true;
            const auto result = event.value(QStringLiteral("result")).toObject();
            response = result.value(QStringLiteral("response")).toString().trimmed();
            if (result.value(QStringLiteral("status")).toString() != QStringLiteral("SUCCESS")) {
                const QString resultError = result.value(QStringLiteral("error")).toString();
                toolError = QStringLiteral("Antigravity result failed: ")
                    + (resultError.isEmpty() ? response : resultError);
                if (toolError.contains(QStringLiteral("quota"), Qt::CaseInsensitive)
                    || toolError.contains(QStringLiteral("RESOURCE_EXHAUSTED"))
                    || toolError.contains(QStringLiteral("Authentication required"), Qt::CaseInsensitive)) {
                    priorityError = toolError;
                }
            }
        }
    }
    if (!priorityError.isEmpty()) {
        return QStringLiteral("__ERROR__: ") + priorityError;
    }
    if (!toolError.isEmpty()) {
        return QStringLiteral("__ERROR__: IMAGE_TOOL_UNAVAILABLE: ") + toolError;
    }
    if (!resultSeen || (requireImage && !imageDone)) {
        return QStringLiteral("__ERROR__: IMAGE_TOOL_UNAVAILABLE: missing completion or successful native generate_image event");
    }
    if (!requireImage && !inspectionDone) {
        return QStringLiteral("__ERROR__: IMAGE_TOOL_UNAVAILABLE: review did not inspect an image with view_file");
    }
    return response;
}
