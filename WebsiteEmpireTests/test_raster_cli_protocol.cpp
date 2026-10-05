#include <QtTest>
#include <QFile>
#include "aicli/CliAntigravity.h"
#include "website/RasterCliProtocol.h"

class Test_Website_RasterCliProtocol : public QObject
{
    Q_OBJECT
private slots:
    void test_rastercli_live_quota_error_overrides_successful_exit();
    void test_rastercli_native_success_requires_completed_tool();
    void test_rastercli_placeholder_claim_is_rejected();
    void test_rastercli_failed_tool_cannot_be_hidden_by_success();
    void test_rastercli_review_extracts_final_verdict();
    void test_rastercli_review_without_inspection_is_rejected();
    void test_rastercli_review_with_filesystem_search_is_rejected();
    void test_rastercli_review_must_inspect_exact_candidate();
    void test_rastercli_result_error_preserves_capacity_and_quota_details();
    void test_rastercli_truncated_or_invalid_stream_fails_closed();
    void test_rastercli_auth_error_overrides_later_errors();
    void test_rastercli_subagent_image_generator_success();
    void test_rastercli_subagent_other_type_is_rejected();
};

void Test_Website_RasterCliProtocol::test_rastercli_live_quota_error_overrides_successful_exit()
{
    const QString fixture = QFINDTESTDATA("fixtures/agy_image_quota.jsonl");
    QVERIFY(!fixture.isEmpty());
    QFile file(fixture);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString result = RasterCliProtocol::decodeAntigravity(file.readAll(), true);
    QVERIFY(result.startsWith(QStringLiteral("__ERROR__")));
    QVERIFY(result.contains(QStringLiteral("RESOURCE_EXHAUSTED")));
    QVERIFY(result.contains(QStringLiteral("2026-09-16T09:37:13Z")));
    CliAntigravity cli;
    QCOMPARE(cli.classifyError(result), CliErrorKind::QuotaExceeded);
}

void Test_Website_RasterCliProtocol::test_rastercli_native_success_requires_completed_tool()
{
    const QByteArray done = R"({"event":"step_update","step_update":{"state":"DONE","step_type":"tool","tool_name":"generate_image","tool_info":{"name":"generate_image"}}}
{"event":"result","result":{"status":"SUCCESS","response":"Saved"}}
)";
    QCOMPARE(RasterCliProtocol::decodeAntigravity(done, true), QStringLiteral("Saved"));
    QByteArray active = done;
    active.replace("DONE", "ACTIVE");
    QVERIFY(RasterCliProtocol::decodeAntigravity(active, true).startsWith(QStringLiteral("__ERROR__")));
}

void Test_Website_RasterCliProtocol::test_rastercli_placeholder_claim_is_rejected()
{
    const QByteArray output = R"({"event":"step_update","step_update":{"state":"DONE","tool_name":"run_command"}}
{"event":"result","result":{"status":"SUCCESS","response":"Generated image with Python; saved output.png"}}
)";
    QVERIFY(RasterCliProtocol::decodeAntigravity(output, true).contains(QStringLiteral("IMAGE_TOOL_UNAVAILABLE")));
}

void Test_Website_RasterCliProtocol::test_rastercli_failed_tool_cannot_be_hidden_by_success()
{
    const QByteArray output = R"({"event":"step_update","step_update":{"state":"ERROR","tool_name":"generate_image","tool_info":{"error":{"message":"service unavailable"}}}}
{"event":"result","result":{"status":"SUCCESS","response":"Saved a fallback image"}}
)";
    const QString result = RasterCliProtocol::decodeAntigravity(output, true);
    QVERIFY(result.startsWith(QStringLiteral("__ERROR__")));
    QVERIFY(result.contains(QStringLiteral("service unavailable")));
}

void Test_Website_RasterCliProtocol::test_rastercli_review_extracts_final_verdict()
{
    const QByteArray output = R"({"event":"step_update","step_update":{"state":"DONE","tool_name":"view_file"}}
{"event":"result","result":{"status":"SUCCESS","response":"FAIL: wrong leggings color"}}
)";
    QCOMPARE(RasterCliProtocol::decodeAntigravity(output, false), QStringLiteral("FAIL: wrong leggings color"));
}

void Test_Website_RasterCliProtocol::test_rastercli_truncated_or_invalid_stream_fails_closed()
{
    const QList<QByteArray> streams = {QByteArray(), QByteArray("OK"), QByteArray("{\"event\":\"init\"}\n"), QByteArray("{broken")};
    for (const auto &stream : streams) {
        QVERIFY(RasterCliProtocol::decodeAntigravity(stream, true).startsWith(QStringLiteral("__ERROR__")));
    }
}

void Test_Website_RasterCliProtocol::test_rastercli_review_without_inspection_is_rejected()
{
    const QByteArray output = R"({"event":"result","result":{"status":"SUCCESS","response":"OK"}}
)";
    QVERIFY(RasterCliProtocol::decodeAntigravity(output, false).startsWith(QStringLiteral("__ERROR__")));
}

void Test_Website_RasterCliProtocol::test_rastercli_review_with_filesystem_search_is_rejected()
{
    const QByteArray output = R"({"event":"step_update","step_update":{"state":"DONE","step_type":"tool","tool_name":"view_file"}}
{"event":"step_update","step_update":{"state":"DONE","step_type":"tool","tool_name":"run_command","tool_info":{"parameters":{"CommandLine":"rg blue ../"}}}}
{"event":"result","result":{"status":"SUCCESS","response":"OK"}}
)";
    QVERIFY(RasterCliProtocol::decodeAntigravity(output, false).startsWith(QStringLiteral("__ERROR__")));
}

void Test_Website_RasterCliProtocol::test_rastercli_review_must_inspect_exact_candidate()
{
    const QByteArray output = R"({"event":"step_update","step_update":{"state":"DONE","step_type":"tool","tool_name":"view_file","tool_info":{"parameters":{"AbsolutePath":"/candidate.jpg"}}}}
{"event":"result","result":{"status":"SUCCESS","response":"OK"}}
)";
    QCOMPARE(RasterCliProtocol::decodeAntigravity(output, false, QStringLiteral("/candidate.jpg")), QStringLiteral("OK"));
    QVERIFY(RasterCliProtocol::decodeAntigravity(output, false, QStringLiteral("/other.jpg")).startsWith(QStringLiteral("__ERROR__")));
}

void Test_Website_RasterCliProtocol::test_rastercli_auth_error_overrides_later_errors()
{
    const QByteArray output = R"({"event":"step_update","step_update":{"state":"ERROR","tool_name":"generate_image","tool_info":{"error":{"message":"Authentication required"}}}}
{"event":"step_update","step_update":{"state":"ERROR","tool_name":"run_command","tool_info":{"error":{"message":"copy failed"}}}}
{"event":"result","result":{"status":"SUCCESS","response":"Done"}}
)";
    CliAntigravity cli;
    QCOMPARE(cli.classifyError(RasterCliProtocol::decodeAntigravity(output, true)), CliErrorKind::AuthRequired);
}

void Test_Website_RasterCliProtocol::test_rastercli_result_error_preserves_capacity_and_quota_details()
{
    const QByteArray output = R"({"event":"result","result":{"status":"ERROR","response":"OK","error":"UNAVAILABLE (code 503): No capacity available for model gemini-3.8-flash-medium"}}
)";
    const QString result = RasterCliProtocol::decodeAntigravity(output, false);
    QVERIFY(result.contains(QStringLiteral("No capacity available")));
    QByteArray quota = output;
    quota.replace("UNAVAILABLE (code 503): No capacity available", "RESOURCE_EXHAUSTED: quota exhausted");
    CliAntigravity cli;
    QCOMPARE(cli.classifyError(RasterCliProtocol::decodeAntigravity(quota, false)), CliErrorKind::QuotaExceeded);
}

void Test_Website_RasterCliProtocol::test_rastercli_subagent_image_generator_success()
{
    const QByteArray output = R"({"event":"step_update","step_update":{"state":"DONE","step_type":"subagent","tool_name":"invoke_subagent","subagent_info":{"subagents":[{"type_name":"image-generator","role":"Fashion Image Generator"}]}}}
{"event":"step_update","step_update":{"state":"DONE","step_type":"tool","tool_name":"run_command","tool_info":{"parameters":{"CommandLine":"cp /tmp/generated.jpg /tmp/output.jpg"}}}}
{"event":"result","result":{"status":"SUCCESS","response":"Image created"}}
)";
    QCOMPARE(RasterCliProtocol::decodeAntigravity(output, true), QStringLiteral("Image created"));
}

void Test_Website_RasterCliProtocol::test_rastercli_subagent_other_type_is_rejected()
{
    const QByteArray output = R"({"event":"step_update","step_update":{"state":"DONE","step_type":"subagent","tool_name":"invoke_subagent","subagent_info":{"subagents":[{"type_name":"research","role":"Codebase Researcher"}]}}}
{"event":"result","result":{"status":"SUCCESS","response":"Done research"}}
)";
    QVERIFY(RasterCliProtocol::decodeAntigravity(output, true).contains(QStringLiteral("IMAGE_TOOL_UNAVAILABLE")));
}

QTEST_GUILESS_MAIN(Test_Website_RasterCliProtocol)
#include "test_raster_cli_protocol.moc"
