#include <QtTest>

#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

namespace {

// Minimal fake bloc overriding only the pure virtuals plus (optionally)
// taxonomy() — used to prove taxonomies()'s default implementation wraps
// taxonomy() correctly without needing a real bloc's storage/render logic.
class FakeBlocNoTaxonomy : public AbstractPageBloc
{
public:
    QString getName() const override { return QStringLiteral("Fake"); }
    void load(const QHash<QString, QString> &) override {}
    void save(QHash<QString, QString> &) const override {}
    AbstractPageBlockWidget *createEditWidget() override { return nullptr; }
    void addCode(QStringView, AbstractEngine &, int, QString &, QString &, QString &,
                QSet<QString> &, QSet<QString> &) const override {}
};

class FakeBlocOneTaxonomy : public FakeBlocNoTaxonomy
{
public:
    std::optional<TaxonomyDescriptor> taxonomy() const override
    {
        return TaxonomyDescriptor{QStringLiteral("fake_id"), QStringLiteral("Fake Display"), true};
    }
};

} // namespace

class Test_Website_AbstractPageBlocTaxonomies : public QObject
{
    Q_OBJECT

private slots:
    void test_abstractpagebloc_taxonomies_default_is_empty_when_no_taxonomy();
    void test_abstractpagebloc_taxonomies_default_wraps_singular_taxonomy();
};

void Test_Website_AbstractPageBlocTaxonomies::test_abstractpagebloc_taxonomies_default_is_empty_when_no_taxonomy()
{
    FakeBlocNoTaxonomy bloc;
    QVERIFY(bloc.taxonomies().isEmpty());
}

void Test_Website_AbstractPageBlocTaxonomies::test_abstractpagebloc_taxonomies_default_wraps_singular_taxonomy()
{
    FakeBlocOneTaxonomy bloc;
    const QList<TaxonomyDescriptor> result = bloc.taxonomies();

    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first().id, QStringLiteral("fake_id"));
    QCOMPARE(result.first().displayName, QStringLiteral("Fake Display"));
    QCOMPARE(result.first().translatable, true);
}

QTEST_MAIN(Test_Website_AbstractPageBlocTaxonomies)
#include "test_abstract_page_bloc_taxonomies.moc"
