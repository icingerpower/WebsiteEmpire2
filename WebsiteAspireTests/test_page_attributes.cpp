#include <QtTest>

class Test_Aspire_Page_Attributes : public QObject
{
    Q_OBJECT

private slots:
    void test_placeholder();
};

void Test_Aspire_Page_Attributes::test_placeholder()
{
    QVERIFY(true);
}

QTEST_MAIN(Test_Aspire_Page_Attributes)
#include "test_page_attributes.moc"
