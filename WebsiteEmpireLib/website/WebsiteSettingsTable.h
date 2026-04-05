#ifndef WEBSITESETTINGSTABLE_H
#define WEBSITESETTINGSTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>

// Table model for global website settings, persisted to settings_global.csv.
//
// Visible columns: Parameter (read-only, translated), Value (editable).
// Each row carries a hidden stable ID accessible via Qt::UserRole.
//
// The CSV stores only Id;Value — the parameter label is derived from the ID
// at runtime via tr(), so renaming a parameter in the UI never breaks
// existing saved data.
//
// Row order, added rows, and deleted rows in the CSV are all handled by
// matching rows to their stable ID rather than by position.
class WebsiteSettingsTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    static constexpr int COL_PARAMETER = 0;
    static constexpr int COL_VALUE     = 1;

    // Returns a QStringList of allowed values for rows that have a constrained
    // vocabulary (e.g. editing_lang_code). Empty list means free-text editing.
    static constexpr int AllowedValuesRole = Qt::UserRole + 1;

    // Stable IDs — never change once data has been saved.
    static const QString ID_WEBSITE_NAME;
    static const QString ID_AUTHOR;
    // The BCP-47 lang code of the language currently being edited in the UI.
    // One domain per language is defined in AbstractEngine; this setting selects
    // which language's content the editor operates on.
    static const QString ID_EDITING_LANG_CODE;

    // Legal information — required for legal page generation.
    // ID_LEGAL_COMPANY_NAME, ID_LEGAL_COMPANY_ADDRESS, ID_LEGAL_REGISTRATION_NO,
    // and ID_LEGAL_CONTACT_EMAIL are mandatory: generateLegalPages() raises
    // ExceptionWithTitleText if any of them is empty.
    // The remaining fields are optional (VAT number, GDPR DPO details).
    static const QString ID_LEGAL_COMPANY_NAME;    ///< Official legal entity name
    static const QString ID_LEGAL_COMPANY_ADDRESS; ///< Full address (street, postal code, city, country)
    static const QString ID_LEGAL_REGISTRATION_NO; ///< Company registration / incorporation number
    static const QString ID_LEGAL_CONTACT_EMAIL;   ///< Legal contact e-mail visible on legal pages
    static const QString ID_LEGAL_VAT_NO;          ///< VAT or tax identification number (optional)
    static const QString ID_LEGAL_DPO_NAME;        ///< GDPR Data Protection Officer name (optional)
    static const QString ID_LEGAL_DPO_EMAIL;       ///< GDPR DPO contact e-mail (optional)

    explicit WebsiteSettingsTable(const QDir &workingDir, QObject *parent = nullptr);

    // Returns the current value for the given stable ID, or empty string if not found.
    QString valueForId(const QString &id) const;

    // Named getters — each returns valueForId() for the corresponding stable ID.
    QString websiteName()      const;
    QString author()           const;
    QString editingLangCode()  const;

    // Legal information getters.
    QString legalCompanyName()    const;
    QString legalCompanyAddress() const;
    QString legalRegistrationNo() const;
    QString legalContactEmail()   const;
    QString legalVatNo()          const;
    QString legalDpoName()        const;
    QString legalDpoEmail()       const;

    // QAbstractItemModel interface
    int           rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int           columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant      data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool          setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct SettingRow {
        QString     id;            // stable machine ID — saved in CSV, used for reload matching
        QString     label;         // translated parameter name — NOT saved, derived from id at construction
        QString     value;         // user-set value — saved in CSV
        QStringList allowedValues; // non-empty → combo box constraint; empty → free text
    };

    void _load();
    void _save();

    QString           m_filePath;
    QList<SettingRow> m_rows;
};

#endif // WEBSITESETTINGSTABLE_H
