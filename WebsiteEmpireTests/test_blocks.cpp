#include <QtTest>

class Test_Website_Blocks : public QObject
{
    Q_OBJECT

private slots:
    void test_placeholder();
};

void Test_Website_Blocks::test_placeholder()
{
    QVERIFY(true);
}

QTEST_MAIN(Test_Website_Blocks)
#include "test_blocks.moc"
