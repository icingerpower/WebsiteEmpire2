#include "LegalPageDefs.h"

#include <QCoreApplication>
#include <QStringList>

// =============================================================================
// Privacy Policy
// =============================================================================

QString LegalPageDefPrivacyPolicy::getId()               const { return QStringLiteral("privacy_policy"); }
QString LegalPageDefPrivacyPolicy::getDisplayName()      const { return QCoreApplication::translate("LegalPageDefs", "Privacy Policy"); }
QString LegalPageDefPrivacyPolicy::getDefaultPermalink() const { return QStringLiteral("/privacy-policy.html"); }

QString LegalPageDefPrivacyPolicy::generateTextContent(const LegalInfo &info) const
{
    QStringList parts;

    parts << QStringLiteral("<h1>Privacy Policy</h1>");

    parts << QStringLiteral("This Privacy Policy explains how <strong>%1</strong> collects, uses, and protects your personal data when you visit <strong>%2</strong>. We are committed to handling your data in accordance with the General Data Protection Regulation (GDPR) and applicable privacy laws.")
                 .arg(info.companyName, info.websiteName);

    parts << QStringLiteral("<h2>1. Data Controller</h2>");

    {
        QString controller = QStringLiteral("<strong>%1</strong><br>%2<br>Company registration: %3<br>Email: %4")
                                 .arg(info.companyName, info.companyAddress, info.registrationNo, info.contactEmail);
        if (!info.vatNo.isEmpty()) {
            controller += QStringLiteral("<br>VAT: %1").arg(info.vatNo);
        }
        parts << controller;
    }

    parts << QStringLiteral("<h2>2. Data We Collect</h2>");

    parts << QStringLiteral("We may collect the following personal data:\n- <strong>Contact data</strong>: name and email address when you contact us voluntarily.\n- <strong>Usage data</strong>: IP address, browser type, pages visited, and referrer URL, collected automatically via server logs and analytics.\n- <strong>Cookie data</strong>: as described in our Cookie Policy.");

    parts << QStringLiteral("<h2>3. Purpose and Legal Basis</h2>");

    parts << QStringLiteral("We process your data for the following purposes:\n- <strong>Service operation</strong> (legal basis: legitimate interests) — to maintain and improve the website.\n- <strong>Communication</strong> (legal basis: consent or contract) — to respond to enquiries you send us.\n- <strong>Legal obligations</strong> (legal basis: compliance with law) — to meet statutory requirements.");

    parts << QStringLiteral("<h2>4. Data Retention</h2>");

    parts << QStringLiteral("We retain personal data only for as long as necessary to fulfil the purposes described above, or as required by law. Contact enquiry data is deleted after three years unless an ongoing relationship requires longer retention.");

    parts << QStringLiteral("<h2>5. Your Rights</h2>");

    parts << QStringLiteral("Under the GDPR you have the right to: access your personal data; rectify inaccurate data; request erasure; restrict or object to processing; data portability; and to withdraw consent at any time without affecting the lawfulness of prior processing.");

    parts << QStringLiteral("To exercise your rights, contact us at: <strong>%1</strong>.").arg(info.contactEmail);

    if (!info.dpoName.isEmpty() || !info.dpoEmail.isEmpty()) {
        QString dpo = QStringLiteral("<h2>6. Data Protection Officer</h2>");
        parts << dpo;
        QString dpoLine;
        if (!info.dpoName.isEmpty()) {
            dpoLine += info.dpoName;
        }
        if (!info.dpoEmail.isEmpty()) {
            if (!dpoLine.isEmpty()) {
                dpoLine += QStringLiteral(" — ");
            }
            dpoLine += info.dpoEmail;
        }
        parts << dpoLine;
    }

    parts << QStringLiteral("<h2>Cookies</h2>");

    parts << QStringLiteral("This website uses cookies. Please refer to our <a href=\"/cookie-policy.html\">Cookie Policy</a> for full details.");

    parts << QStringLiteral("<h2>Changes to This Policy</h2>");

    parts << QStringLiteral("We may update this Privacy Policy from time to time. The current version is always available on this page. We encourage you to review it periodically.");

    return parts.join(QStringLiteral("\n\n"));
}

DECLARE_LEGAL_PAGE(LegalPageDefPrivacyPolicy)

// =============================================================================
// Terms of Service
// =============================================================================

QString LegalPageDefTermsOfService::getId()               const { return QStringLiteral("terms_of_service"); }
QString LegalPageDefTermsOfService::getDisplayName()      const { return QCoreApplication::translate("LegalPageDefs", "Terms of Service"); }
QString LegalPageDefTermsOfService::getDefaultPermalink() const { return QStringLiteral("/terms-of-service.html"); }

QString LegalPageDefTermsOfService::generateTextContent(const LegalInfo &info) const
{
    QStringList parts;

    parts << QStringLiteral("<h1>Terms of Service</h1>");

    parts << QStringLiteral("Please read these Terms of Service carefully before using <strong>%1</strong> operated by <strong>%2</strong>. By accessing or using the website, you agree to be bound by these terms.")
                 .arg(info.websiteName, info.companyName);

    parts << QStringLiteral("<h2>1. Acceptance of Terms</h2>");

    parts << QStringLiteral("By using this website, you confirm that you are at least 18 years of age, have read and understood these Terms, and agree to comply with them. If you do not agree, please discontinue use of the website immediately.");

    parts << QStringLiteral("<h2>2. Services Provided</h2>");

    parts << QStringLiteral("%1 provides information and services as described on this website. We reserve the right to modify, suspend, or discontinue any part of the website at any time without prior notice.")
                 .arg(info.companyName);

    parts << QStringLiteral("<h2>3. Intellectual Property</h2>");

    parts << QStringLiteral("All content on this website — including text, images, graphics, logos, and software — is the property of %1 or its licensors and is protected by applicable intellectual property laws. You may not reproduce, distribute, or create derivative works without our express written permission.")
                 .arg(info.companyName);

    parts << QStringLiteral("<h2>4. User Conduct</h2>");

    parts << QStringLiteral("You agree not to use this website to: post or transmit unlawful, harmful, or misleading content; attempt to gain unauthorised access to any system; interfere with the website's operation; or engage in any activity that violates applicable law.");

    parts << QStringLiteral("<h2>5. Limitation of Liability</h2>");

    parts << QStringLiteral("To the fullest extent permitted by law, %1 shall not be liable for any indirect, incidental, special, or consequential damages arising from your use of this website or reliance on its content, even if we have been advised of the possibility of such damages.")
                 .arg(info.companyName);

    parts << QStringLiteral("<h2>6. Third-Party Links</h2>");

    parts << QStringLiteral("This website may contain links to third-party websites. These links are provided for convenience only. We have no control over the content of those sites and accept no responsibility for them or for any loss or damage arising from your use of them.");

    parts << QStringLiteral("<h2>7. Changes to Terms</h2>");

    parts << QStringLiteral("We reserve the right to update these Terms at any time. Changes become effective upon publication on this page. Continued use of the website after changes are posted constitutes your acceptance of the revised Terms.");

    parts << QStringLiteral("<h2>8. Governing Law</h2>");

    parts << QStringLiteral("These Terms shall be governed by and construed in accordance with the laws applicable to %1, whose registered address is %2. Any disputes arising shall be subject to the exclusive jurisdiction of the competent courts in that jurisdiction.")
                 .arg(info.companyName, info.companyAddress);

    parts << QStringLiteral("<h2>Contact</h2>");

    parts << QStringLiteral("For any questions regarding these Terms, please contact us at: <strong>%1</strong>.").arg(info.contactEmail);

    return parts.join(QStringLiteral("\n\n"));
}

DECLARE_LEGAL_PAGE(LegalPageDefTermsOfService)

// =============================================================================
// Cookie Policy
// =============================================================================

QString LegalPageDefCookiePolicy::getId()               const { return QStringLiteral("cookie_policy"); }
QString LegalPageDefCookiePolicy::getDisplayName()      const { return QCoreApplication::translate("LegalPageDefs", "Cookie Policy"); }
QString LegalPageDefCookiePolicy::getDefaultPermalink() const { return QStringLiteral("/cookie-policy.html"); }

QString LegalPageDefCookiePolicy::generateTextContent(const LegalInfo &info) const
{
    QStringList parts;

    parts << QStringLiteral("<h1>Cookie Policy</h1>");

    parts << QStringLiteral("This Cookie Policy explains how <strong>%1</strong> uses cookies and similar tracking technologies on <strong>%2</strong>.")
                 .arg(info.companyName, info.websiteName);

    parts << QStringLiteral("<h2>1. What Are Cookies?</h2>");

    parts << QStringLiteral("Cookies are small text files placed on your device when you visit a website. They allow the site to recognise your device and remember information about your visit, such as your preferences or actions.");

    parts << QStringLiteral("<h2>2. Cookies We Use</h2>");

    parts << QStringLiteral("<strong>Essential cookies</strong> — required for the website to function correctly (e.g. security, session management). These cannot be disabled.");

    parts << QStringLiteral("<strong>Analytics cookies</strong> — help us understand how visitors interact with our website by collecting anonymous usage data. We use this information to improve the website experience.");

    parts << QStringLiteral("<strong>Preference cookies</strong> — remember your settings and choices (e.g. language, display options) to provide a more personalised experience.");

    parts << QStringLiteral("We do not use advertising or tracking cookies that follow you across other websites.");

    parts << QStringLiteral("<h2>3. Your Consent</h2>");

    parts << QStringLiteral("When you first visit our website, you are asked to consent to non-essential cookies. You can change your preferences at any time by clearing your browser cookies and revisiting the site.");

    parts << QStringLiteral("<h2>4. How to Manage Cookies</h2>");

    parts << QStringLiteral("You can control and delete cookies through your browser settings. Please note that disabling cookies may affect the functionality of this and other websites you visit. Refer to your browser's help documentation for instructions.");

    parts << QStringLiteral("<h2>5. Changes to This Policy</h2>");

    parts << QStringLiteral("We may update this Cookie Policy to reflect changes in technology or legislation. The current version is always available on this page.");

    parts << QStringLiteral("<h2>Contact</h2>");

    parts << QStringLiteral("For questions about our use of cookies, contact us at: <strong>%1</strong>.").arg(info.contactEmail);

    return parts.join(QStringLiteral("\n\n"));
}

DECLARE_LEGAL_PAGE(LegalPageDefCookiePolicy)

// =============================================================================
// Legal Notice
// =============================================================================

QString LegalPageDefLegalNotice::getId()               const { return QStringLiteral("legal_notice"); }
QString LegalPageDefLegalNotice::getDisplayName()      const { return QCoreApplication::translate("LegalPageDefs", "Legal Notice"); }
QString LegalPageDefLegalNotice::getDefaultPermalink() const { return QStringLiteral("/legal-notice.html"); }

QString LegalPageDefLegalNotice::generateTextContent(const LegalInfo &info) const
{
    QStringList parts;

    parts << QStringLiteral("<h1>Legal Notice</h1>");

    parts << QStringLiteral("<h2>Publisher</h2>");

    {
        QString publisher = QStringLiteral("<strong>%1</strong><br>%2<br>Company registration number: %3<br>Email: %4")
                                .arg(info.companyName, info.companyAddress, info.registrationNo, info.contactEmail);
        if (!info.vatNo.isEmpty()) {
            publisher += QStringLiteral("<br>VAT identification number: %1").arg(info.vatNo);
        }
        parts << publisher;
    }

    parts << QStringLiteral("<h2>Website</h2>");

    parts << QStringLiteral("Website name: <strong>%1</strong>").arg(info.websiteName);

    parts << QStringLiteral("<h2>Hosting</h2>");

    parts << QStringLiteral("[Please fill in the name and address of the hosting provider.]");

    parts << QStringLiteral("<h2>Intellectual Property</h2>");

    parts << QStringLiteral("All content published on this website (text, images, graphics, videos, etc.) is the exclusive property of %1, unless otherwise stated, and is protected under applicable intellectual property laws. Any reproduction, representation, modification, or adaptation — in whole or in part — is strictly prohibited without prior written consent.")
                 .arg(info.companyName);

    parts << QStringLiteral("<h2>Limitation of Liability</h2>");

    parts << QStringLiteral("%1 takes all reasonable steps to ensure the accuracy and currency of information published on this website, but makes no warranty as to its completeness or fitness for any particular purpose. %1 shall not be liable for any direct or indirect damage resulting from access to, or use of, this website.")
                 .arg(info.companyName);

    parts << QStringLiteral("<h2>Privacy</h2>");

    parts << QStringLiteral("For information on how we handle your personal data, please refer to our <a href=\"/privacy-policy.html\">Privacy Policy</a>.");

    if (!info.dpoName.isEmpty() || !info.dpoEmail.isEmpty()) {
        parts << QStringLiteral("<h2>Data Protection Officer</h2>");
        QString dpoLine;
        if (!info.dpoName.isEmpty()) {
            dpoLine += info.dpoName;
        }
        if (!info.dpoEmail.isEmpty()) {
            if (!dpoLine.isEmpty()) {
                dpoLine += QStringLiteral(" — ");
            }
            dpoLine += info.dpoEmail;
        }
        parts << dpoLine;
    }

    parts << QStringLiteral("<h2>Contact</h2>");

    parts << QStringLiteral("For any enquiries regarding this website, please contact: <strong>%1</strong>.").arg(info.contactEmail);

    return parts.join(QStringLiteral("\n\n"));
}

DECLARE_LEGAL_PAGE(LegalPageDefLegalNotice)

// =============================================================================
// Contact Us
// =============================================================================

QString LegalPageDefContactUs::getId()               const { return QStringLiteral("contact_us"); }
QString LegalPageDefContactUs::getDisplayName()      const { return QCoreApplication::translate("LegalPageDefs", "Contact Us"); }
QString LegalPageDefContactUs::getDefaultPermalink() const { return QStringLiteral("/contact-us.html"); }

QString LegalPageDefContactUs::generateTextContent(const LegalInfo &info) const
{
    QStringList parts;

    parts << QStringLiteral("<h1>Contact Us</h1>");

    parts << QStringLiteral("We'd love to hear from you. Whether you have a question, a suggestion, or simply want to get in touch, please don't hesitate to reach out to <strong>%1</strong>.").arg(info.companyName);

    parts << QStringLiteral("<h2>Contact Details</h2>");

    {
        QString details = QStringLiteral("<strong>%1</strong><br>%2<br>Email: <a href=\"mailto:%3\">%3</a>")
                              .arg(info.companyName, info.companyAddress, info.contactEmail);
        parts << details;
    }

    parts << QStringLiteral("<h2>Response Time</h2>");

    parts << QStringLiteral("We aim to respond to all enquiries within 2 business days. For urgent matters, please indicate this clearly in the subject line of your email.");

    parts << QStringLiteral("<h2>Privacy</h2>");

    parts << QStringLiteral("Any personal data you provide when contacting us will be used solely to respond to your enquiry and will be handled in accordance with our <a href=\"/privacy-policy.html\">Privacy Policy</a>.");

    return parts.join(QStringLiteral("\n\n"));
}

DECLARE_LEGAL_PAGE(LegalPageDefContactUs)

// =============================================================================
// About Us
// =============================================================================

QString LegalPageDefAboutUs::getId()               const { return QStringLiteral("about_us"); }
QString LegalPageDefAboutUs::getDisplayName()      const { return QCoreApplication::translate("LegalPageDefs", "About Us"); }
QString LegalPageDefAboutUs::getDefaultPermalink() const { return QStringLiteral("/about-us.html"); }

QString LegalPageDefAboutUs::generateTextContent(const LegalInfo &info) const
{
    QStringList parts;

    parts << QStringLiteral("<h1>About Us</h1>");

    parts << QStringLiteral("Welcome to <strong>%1</strong>. This page tells you a little about who we are and what we do.")
                 .arg(info.websiteName);

    parts << QStringLiteral("<h2>Who We Are</h2>");

    parts << QStringLiteral("<strong>%1</strong> is a company based at %2. [Please add a short description of your company, its history, and its core activity here.]")
                 .arg(info.companyName, info.companyAddress);

    parts << QStringLiteral("<h2>Our Mission</h2>");

    parts << QStringLiteral("[Please describe your mission, values, and what sets you apart from competitors.]");

    parts << QStringLiteral("<h2>Get in Touch</h2>");

    parts << QStringLiteral("Have questions or want to know more? We're always happy to talk. Reach us at <a href=\"mailto:%1\">%1</a> or visit our <a href=\"/contact-us.html\">Contact Us</a> page.")
                 .arg(info.contactEmail);

    return parts.join(QStringLiteral("\n\n"));
}

DECLARE_LEGAL_PAGE(LegalPageDefAboutUs)
