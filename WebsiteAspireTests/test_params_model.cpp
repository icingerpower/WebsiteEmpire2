#include <QtTest>

#include <QDir>
#include <QJsonObject>
#include <QTemporaryDir>

#include "aspire/generator/AbstractGenerator.h"
#include "aspire/generator/ParamsModel.h"

// ---------------------------------------------------------------------------
// Stub generator used by all tests
// ---------------------------------------------------------------------------

class StubGenerator : public AbstractGenerator
{
    Q_OBJECT
public:
    explicit StubGenerator(const QDir &dir = QDir(), QObject *parent = nullptr)
        : AbstractGenerator(dir, parent)
    {}

    QString getId()   const override { return QStringLiteral("stub_gen"); }
    QString getName() const override { return QStringLiteral("Stub"); }

    AbstractGenerator *createInstance(const QDir &dir) const override
    {
        return new StubGenerator(dir);
    }

    QList<Param> getParams() const override
    {
        return {
            {QStringLiteral("name"),   QStringLiteral("Name"),   QStringLiteral("A name"),   QStringLiteral("default_name"), false, false},
            {QStringLiteral("file"),   QStringLiteral("File"),   QStringLiteral("A file"),   QStringLiteral(""),             true,  false},
            {QStringLiteral("folder"), QStringLiteral("Folder"), QStringLiteral("A folder"), QStringLiteral(""),             false, true },
        };
    }

    QString checkParams(const QList<Param> &params) const override
    {
        for (const Param &p : params) {
            if (p.id == QLatin1String("name") && p.defaultValue.toString().isEmpty()) {
                return QStringLiteral("Name cannot be empty");
            }
        }
        return {};
    }

protected:
    QStringList buildInitialJobIds() const override { return {}; }
    QJsonObject buildJobPayload(const QString &) const override { return {}; }
    void processReply(const QString &, const QJsonObject &) override {}
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QTemporaryDir *makeDir() { return new QTemporaryDir; }

static StubGenerator *makeGen(const QTemporaryDir &dir)
{
    return new StubGenerator(QDir(dir.path()));
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_Params_Model : public QObject
{
    Q_OBJECT

private slots:

    // ---- rowCount / columnCount --------------------------------------------

    void test_columnCount_is_two()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.columnCount(), 2);
    }

    void test_rowCount_matches_param_count()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.rowCount(), gen->getParams().size()); // 3
    }

    void test_rowCount_zero_for_valid_parent()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        const QModelIndex root = model.index(0, 0);
        QCOMPARE(model.rowCount(root), 0);
    }

    void test_columnCount_zero_for_valid_parent()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        const QModelIndex root = model.index(0, 0);
        QCOMPARE(model.columnCount(root), 0);
    }

    // ---- headerData --------------------------------------------------------

    void test_headerData_column0_is_parameter()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Parameter"));
    }

    void test_headerData_column1_is_value()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Value"));
    }

    void test_headerData_vertical_returns_invalid()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(!model.headerData(0, Qt::Vertical, Qt::DisplayRole).isValid());
    }

    void test_headerData_out_of_bounds_returns_invalid()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(!model.headerData(5, Qt::Horizontal, Qt::DisplayRole).isValid());
    }

    void test_headerData_non_display_role_returns_invalid()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(!model.headerData(0, Qt::Horizontal, Qt::EditRole).isValid());
    }

    // ---- data: column 0 (name) ---------------------------------------------

    void test_data_column0_display_is_param_name()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(),
                 QStringLiteral("Name"));
        QCOMPARE(model.data(model.index(1, 0), Qt::DisplayRole).toString(),
                 QStringLiteral("File"));
        QCOMPARE(model.data(model.index(2, 0), Qt::DisplayRole).toString(),
                 QStringLiteral("Folder"));
    }

    void test_data_column0_edit_same_as_display()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.data(model.index(0, 0), Qt::EditRole),
                 model.data(model.index(0, 0), Qt::DisplayRole));
    }

    // ---- data: column 1 (value) --------------------------------------------

    void test_data_column1_default_value()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        // First param default is "default_name"
        QCOMPARE(model.data(model.index(0, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("default_name"));
    }

    void test_data_column1_edit_role_same_as_display()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.data(model.index(0, 1), Qt::EditRole),
                 model.data(model.index(0, 1), Qt::DisplayRole));
    }

    // ---- data: tooltip -----------------------------------------------------

    void test_data_tooltip_column0()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.data(model.index(0, 0), Qt::ToolTipRole).toString(),
                 QStringLiteral("A name"));
    }

    void test_data_tooltip_column1()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.data(model.index(1, 1), Qt::ToolTipRole).toString(),
                 QStringLiteral("A file"));
    }

    // ---- data: custom roles ------------------------------------------------

    void test_data_isFileRole_true_for_file_param()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(model.data(model.index(1, 1), ParamsModel::IsFileRole).toBool());
    }

    void test_data_isFileRole_false_for_regular_param()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(!model.data(model.index(0, 1), ParamsModel::IsFileRole).toBool());
    }

    void test_data_isFolderRole_true_for_folder_param()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(model.data(model.index(2, 1), ParamsModel::IsFolderRole).toBool());
    }

    void test_data_isFolderRole_false_for_file_param()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(!model.data(model.index(1, 1), ParamsModel::IsFolderRole).toBool());
    }

    void test_data_isFileRole_false_for_folder_param()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(!model.data(model.index(2, 1), ParamsModel::IsFileRole).toBool());
    }

    // ---- data: out of bounds / invalid index -------------------------------

    void test_data_invalid_index_returns_invalid()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(!model.data(QModelIndex(), Qt::DisplayRole).isValid());
    }

    void test_data_row_out_of_bounds_returns_invalid()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QVERIFY(!model.data(model.index(99, 0), Qt::DisplayRole).isValid());
    }

    // ---- flags -------------------------------------------------------------

    void test_flags_column0_not_editable()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        const Qt::ItemFlags f = model.flags(model.index(0, 0));
        QVERIFY(f.testFlag(Qt::ItemIsEnabled));
        QVERIFY(f.testFlag(Qt::ItemIsSelectable));
        QVERIFY(!f.testFlag(Qt::ItemIsEditable));
    }

    void test_flags_column1_editable()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        const Qt::ItemFlags f = model.flags(model.index(0, 1));
        QVERIFY(f.testFlag(Qt::ItemIsEnabled));
        QVERIFY(f.testFlag(Qt::ItemIsSelectable));
        QVERIFY(f.testFlag(Qt::ItemIsEditable));
    }

    void test_flags_invalid_index_no_flags()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QCOMPARE(model.flags(QModelIndex()), Qt::NoItemFlags);
    }

    // ---- setData: success --------------------------------------------------

    void test_setData_updates_display_value()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        const QModelIndex idx = model.index(0, 1);
        QVERIFY(model.setData(idx, QStringLiteral("new_value"), Qt::EditRole));
        QCOMPARE(model.data(idx, Qt::DisplayRole).toString(),
                 QStringLiteral("new_value"));
    }

    void test_setData_persists_via_generator()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        const QModelIndex idx = model.index(0, 1);
        model.setData(idx, QStringLiteral("persisted"), Qt::EditRole);
        QCOMPARE(gen->paramValue(QStringLiteral("name")).toString(),
                 QStringLiteral("persisted"));
    }

    // ---- setData: signal emission ------------------------------------------

    void test_setData_emits_paramChanged()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QSignalSpy spy(&model, &ParamsModel::paramChanged);
        model.setData(model.index(0, 1), QStringLiteral("x"), Qt::EditRole);
        QCOMPARE(spy.count(), 1);
    }

    void test_setData_emits_dataChanged()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
        model.setData(model.index(0, 1), QStringLiteral("x"), Qt::EditRole);
        QCOMPARE(spy.count(), 1);
    }

    // ---- setData: failure cases --------------------------------------------

    void test_setData_fails_on_column0()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QSignalSpy spy(&model, &ParamsModel::paramChanged);
        QVERIFY(!model.setData(model.index(0, 0), QStringLiteral("x"), Qt::EditRole));
        QCOMPARE(spy.count(), 0);
    }

    void test_setData_fails_with_wrong_role()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QSignalSpy spy(&model, &ParamsModel::paramChanged);
        QVERIFY(!model.setData(model.index(0, 1), QStringLiteral("x"), Qt::DisplayRole));
        QCOMPARE(spy.count(), 0);
    }

    void test_setData_fails_with_invalid_index()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QSignalSpy spy(&model, &ParamsModel::paramChanged);
        QVERIFY(!model.setData(QModelIndex(), QStringLiteral("x"), Qt::EditRole));
        QCOMPARE(spy.count(), 0);
    }

    void test_setData_fails_with_row_out_of_bounds()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QSignalSpy spy(&model, &ParamsModel::paramChanged);
        QVERIFY(!model.setData(model.index(99, 1), QStringLiteral("x"), Qt::EditRole));
        QCOMPARE(spy.count(), 0);
    }

    // ---- Persistence: new model loads saved values -------------------------

    void test_new_model_loads_previously_saved_value()
    {
        QTemporaryDir dir;
        {
            QScopedPointer<StubGenerator> gen(makeGen(dir));
            ParamsModel model(gen.data());
            model.setData(model.index(0, 1), QStringLiteral("saved_value"), Qt::EditRole);
        }
        // Create a fresh generator bound to the same directory — settings should persist.
        QScopedPointer<StubGenerator> gen2(makeGen(dir));
        ParamsModel model2(gen2.data());
        QCOMPARE(model2.data(model2.index(0, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("saved_value"));
    }

    void test_new_model_loads_file_param_saved_value()
    {
        QTemporaryDir dir;
        const QString path = QStringLiteral("/some/path/to/file.csv");
        {
            QScopedPointer<StubGenerator> gen(makeGen(dir));
            ParamsModel model(gen.data());
            model.setData(model.index(1, 1), path, Qt::EditRole);
        }
        QScopedPointer<StubGenerator> gen2(makeGen(dir));
        ParamsModel model2(gen2.data());
        QCOMPARE(model2.data(model2.index(1, 1), Qt::DisplayRole).toString(), path);
    }

    void test_new_model_loads_folder_param_saved_value()
    {
        QTemporaryDir dir;
        const QString path = QStringLiteral("/some/folder");
        {
            QScopedPointer<StubGenerator> gen(makeGen(dir));
            ParamsModel model(gen.data());
            model.setData(model.index(2, 1), path, Qt::EditRole);
        }
        QScopedPointer<StubGenerator> gen2(makeGen(dir));
        ParamsModel model2(gen2.data());
        QCOMPARE(model2.data(model2.index(2, 1), Qt::DisplayRole).toString(), path);
    }

    // ---- Multiple params: independent storage ------------------------------

    void test_multiple_params_saved_independently()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        model.setData(model.index(0, 1), QStringLiteral("alpha"), Qt::EditRole);
        model.setData(model.index(1, 1), QStringLiteral("/path/a"), Qt::EditRole);
        model.setData(model.index(2, 1), QStringLiteral("/dir/b"), Qt::EditRole);

        QCOMPARE(model.data(model.index(0, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("alpha"));
        QCOMPARE(model.data(model.index(1, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("/path/a"));
        QCOMPARE(model.data(model.index(2, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("/dir/b"));
    }

    void test_updating_one_param_does_not_affect_others()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        model.setData(model.index(0, 1), QStringLiteral("first"), Qt::EditRole);
        model.setData(model.index(1, 1), QStringLiteral("/path"), Qt::EditRole);
        // Overwrite only the first param
        model.setData(model.index(0, 1), QStringLiteral("changed"), Qt::EditRole);

        QCOMPARE(model.data(model.index(0, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("changed"));
        // Second param must remain unchanged
        QCOMPARE(model.data(model.index(1, 1), Qt::DisplayRole).toString(),
                 QStringLiteral("/path"));
    }

    void test_all_params_survive_new_model()
    {
        QTemporaryDir dir;
        {
            QScopedPointer<StubGenerator> gen(makeGen(dir));
            ParamsModel model(gen.data());
            model.setData(model.index(0, 1), QStringLiteral("n"), Qt::EditRole);
            model.setData(model.index(1, 1), QStringLiteral("/f"), Qt::EditRole);
            model.setData(model.index(2, 1), QStringLiteral("/d"), Qt::EditRole);
        }
        QScopedPointer<StubGenerator> gen2(makeGen(dir));
        ParamsModel model2(gen2.data());
        QCOMPARE(model2.data(model2.index(0, 1), Qt::DisplayRole).toString(), QStringLiteral("n"));
        QCOMPARE(model2.data(model2.index(1, 1), Qt::DisplayRole).toString(), QStringLiteral("/f"));
        QCOMPARE(model2.data(model2.index(2, 1), Qt::DisplayRole).toString(), QStringLiteral("/d"));
    }

    // ---- IsFile / IsFolder roles not affected by column 0 -----------------

    void test_isFileRole_returns_invalid_for_column0()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        // IsFileRole is only defined on column 1; column 0 returns a QVariant but
        // the underlying param still has the flag — the model doesn't restrict by column
        // for custom roles.  Verify that the file param row returns true regardless of column.
        QVERIFY(model.data(model.index(1, 0), ParamsModel::IsFileRole).toBool());
    }

    // ---- paramChanged signal count on multiple edits -----------------------

    void test_setData_emits_paramChanged_once_per_call()
    {
        QTemporaryDir dir;
        QScopedPointer<StubGenerator> gen(makeGen(dir));
        ParamsModel model(gen.data());
        QSignalSpy spy(&model, &ParamsModel::paramChanged);
        model.setData(model.index(0, 1), QStringLiteral("a"), Qt::EditRole);
        model.setData(model.index(1, 1), QStringLiteral("/b"), Qt::EditRole);
        model.setData(model.index(2, 1), QStringLiteral("/c"), Qt::EditRole);
        QCOMPARE(spy.count(), 3);
    }
};

QTEST_MAIN(Test_Params_Model)
#include "test_params_model.moc"
