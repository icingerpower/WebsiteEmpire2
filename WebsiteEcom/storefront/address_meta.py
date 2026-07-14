"""
Address metadata for country-adaptive checkout (ADR-017).

Single source of truth for:
  - Which countries use a subdivision select vs. free text vs. no subdivision
  - Subdivision labels and choices (bare ISO 3166-2 suffix codes)
  - Postal code labels, validation patterns and canonical examples

§XV-4 (single resolution function): the form, the template, the AJAX endpoint
and the tests all call get_country_meta(). No second copy of a postal regex
anywhere else.

§XV-3 (state explicit, never inferred): every country in this module has an
explicit subdivision_mode; DEFAULT_META covers every unlisted country as
'text' (today's v1 behavior), not as a silent missing-key exception.

This module has NO Django model imports — it is pure data + functions so it
loads before the ORM and is trivially unit-testable without a database.
"""

import re
import unicodedata
from dataclasses import dataclass
from typing import Optional

from django.utils.translation import gettext_lazy as _


# ---------------------------------------------------------------------------
# Dataclass
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class CountryAddressMeta:
    """
    Immutable address rules for one country.

    country:           ISO 3166-1 alpha-2 (the dict key); '' for DEFAULT_META.
    subdivision_mode:  'select' — dropdown required (subdivisions non-empty)
                       'text'   — optional free-text (today's v1 behaviour)
                       'none'   — field hidden, stored as ''
    subdivisions:      tuple of (bare_code, label) — bare ISO 3166-2 suffix
                       codes without the country prefix (e.g. 'NY', not 'US-NY').
                       Empty for text/none modes.
    subdivision_label: Translated UI label for the subdivision field.
    postal_label:      Translated UI label for the postal code field.
    postal_pattern:    Anchored regex applied after normalize_postal_code(); None
                       means non-empty check only.
    postal_example:    Placeholder/hint shown in the postal code input.
    """

    country: str
    subdivision_mode: str
    subdivisions: tuple
    subdivision_label: object  # gettext_lazy proxy
    postal_label: object       # gettext_lazy proxy
    postal_pattern: Optional[str]
    postal_example: str


# ---------------------------------------------------------------------------
# Subdivision datasets (module-level for readability)
# ---------------------------------------------------------------------------

_US_STATES = (
    ("AL", "Alabama"), ("AK", "Alaska"), ("AZ", "Arizona"), ("AR", "Arkansas"),
    ("CA", "California"), ("CO", "Colorado"), ("CT", "Connecticut"), ("DE", "Delaware"),
    ("FL", "Florida"), ("GA", "Georgia"), ("HI", "Hawaii"), ("ID", "Idaho"),
    ("IL", "Illinois"), ("IN", "Indiana"), ("IA", "Iowa"), ("KS", "Kansas"),
    ("KY", "Kentucky"), ("LA", "Louisiana"), ("ME", "Maine"), ("MD", "Maryland"),
    ("MA", "Massachusetts"), ("MI", "Michigan"), ("MN", "Minnesota"), ("MS", "Mississippi"),
    ("MO", "Missouri"), ("MT", "Montana"), ("NE", "Nebraska"), ("NV", "Nevada"),
    ("NH", "New Hampshire"), ("NJ", "New Jersey"), ("NM", "New Mexico"), ("NY", "New York"),
    ("NC", "North Carolina"), ("ND", "North Dakota"), ("OH", "Ohio"), ("OK", "Oklahoma"),
    ("OR", "Oregon"), ("PA", "Pennsylvania"), ("RI", "Rhode Island"), ("SC", "South Carolina"),
    ("SD", "South Dakota"), ("TN", "Tennessee"), ("TX", "Texas"), ("UT", "Utah"),
    ("VT", "Vermont"), ("VA", "Virginia"), ("WA", "Washington"), ("WV", "West Virginia"),
    ("WI", "Wisconsin"), ("WY", "Wyoming"), ("DC", "District of Columbia"),
)

_CA_PROVINCES = (
    ("AB", "Alberta"), ("BC", "British Columbia"), ("MB", "Manitoba"),
    ("NB", "New Brunswick"), ("NL", "Newfoundland and Labrador"),
    ("NS", "Nova Scotia"), ("NT", "Northwest Territories"), ("NU", "Nunavut"),
    ("ON", "Ontario"), ("PE", "Prince Edward Island"), ("QC", "Quebec"),
    ("SK", "Saskatchewan"), ("YT", "Yukon"),
)

_AU_STATES = (
    ("ACT", "Australian Capital Territory"), ("NSW", "New South Wales"),
    ("NT", "Northern Territory"), ("QLD", "Queensland"),
    ("SA", "South Australia"), ("TAS", "Tasmania"),
    ("VIC", "Victoria"), ("WA", "Western Australia"),
)

_MX_STATES = (
    ("AG", "Aguascalientes"), ("BC", "Baja California"), ("BS", "Baja California Sur"),
    ("CM", "Campeche"), ("CS", "Chiapas"), ("CH", "Chihuahua"), ("CO", "Coahuila"),
    ("CL", "Colima"), ("DF", "Ciudad de México"), ("DG", "Durango"),
    ("GT", "Guanajuato"), ("GR", "Guerrero"), ("HG", "Hidalgo"),
    ("JA", "Jalisco"), ("EM", "Estado de México"), ("MI", "Michoacán"),
    ("MO", "Morelos"), ("NA", "Nayarit"), ("NL", "Nuevo León"),
    ("OA", "Oaxaca"), ("PU", "Puebla"), ("QT", "Querétaro"),
    ("QR", "Quintana Roo"), ("SL", "San Luis Potosí"), ("SI", "Sinaloa"),
    ("SO", "Sonora"), ("TB", "Tabasco"), ("TM", "Tamaulipas"),
    ("TL", "Tlaxcala"), ("VE", "Veracruz"), ("YU", "Yucatán"),
    ("ZA", "Zacatecas"),
)

_BR_STATES = (
    ("AC", "Acre"), ("AL", "Alagoas"), ("AP", "Amapá"), ("AM", "Amazonas"),
    ("BA", "Bahia"), ("CE", "Ceará"), ("DF", "Distrito Federal"),
    ("ES", "Espírito Santo"), ("GO", "Goiás"), ("MA", "Maranhão"),
    ("MT", "Mato Grosso"), ("MS", "Mato Grosso do Sul"), ("MG", "Minas Gerais"),
    ("PA", "Pará"), ("PB", "Paraíba"), ("PR", "Paraná"), ("PE", "Pernambuco"),
    ("PI", "Piauí"), ("RJ", "Rio de Janeiro"), ("RN", "Rio Grande do Norte"),
    ("RS", "Rio Grande do Sul"), ("RO", "Rondônia"), ("RR", "Roraima"),
    ("SC", "Santa Catarina"), ("SP", "São Paulo"), ("SE", "Sergipe"),
    ("TO", "Tocantins"),
)

_ES_COMMUNITIES = (
    ("AN", "Andalucía"), ("AR", "Aragón"), ("AS", "Asturias"),
    ("IB", "Islas Baleares"), ("CN", "Canarias"), ("CB", "Cantabria"),
    ("CL", "Castilla y León"), ("CM", "Castilla-La Mancha"), ("CT", "Cataluña"),
    ("CE", "Ceuta"), ("EX", "Extremadura"), ("GA", "Galicia"),
    ("LO", "La Rioja"), ("MD", "Madrid"), ("ME", "Melilla"),
    ("MC", "Región de Murcia"), ("NC", "Comunidad Foral de Navarra"),
    ("VC", "Comunitat Valenciana"), ("PV", "País Vasco"),
)

_IT_PROVINCES = (
    ("AG", "Agrigento"), ("AL", "Alessandria"), ("AN", "Ancona"), ("AO", "Aosta"),
    ("AP", "Ascoli Piceno"), ("AQ", "L'Aquila"), ("AR", "Arezzo"), ("AT", "Asti"),
    ("AV", "Avellino"), ("BA", "Bari"), ("BG", "Bergamo"), ("BI", "Biella"),
    ("BL", "Belluno"), ("BN", "Benevento"), ("BO", "Bologna"), ("BR", "Brindisi"),
    ("BS", "Brescia"), ("BT", "Barletta-Andria-Trani"), ("BZ", "Bolzano"),
    ("CA", "Cagliari"), ("CB", "Campobasso"), ("CE", "Caserta"), ("CH", "Chieti"),
    ("CL", "Caltanissetta"), ("CN", "Cuneo"), ("CO", "Como"), ("CR", "Cremona"),
    ("CS", "Cosenza"), ("CT", "Catania"), ("CZ", "Catanzaro"), ("EN", "Enna"),
    ("FC", "Forlì-Cesena"), ("FE", "Ferrara"), ("FG", "Foggia"), ("FI", "Firenze"),
    ("FM", "Fermo"), ("FR", "Frosinone"), ("GE", "Genova"), ("GO", "Gorizia"),
    ("GR", "Grosseto"), ("IM", "Imperia"), ("IS", "Isernia"), ("KR", "Crotone"),
    ("LC", "Lecco"), ("LE", "Lecce"), ("LI", "Livorno"), ("LO", "Lodi"),
    ("LT", "Latina"), ("LU", "Lucca"), ("MB", "Monza e Brianza"), ("MC", "Macerata"),
    ("ME", "Messina"), ("MI", "Milano"), ("MN", "Mantova"), ("MO", "Modena"),
    ("MS", "Massa-Carrara"), ("MT", "Matera"), ("NA", "Napoli"), ("NO", "Novara"),
    ("NU", "Nuoro"), ("OR", "Oristano"), ("PA", "Palermo"), ("PC", "Piacenza"),
    ("PD", "Padova"), ("PE", "Pescara"), ("PG", "Perugia"), ("PI", "Pisa"),
    ("PN", "Pordenone"), ("PO", "Prato"), ("PR", "Parma"), ("PT", "Pistoia"),
    ("PU", "Pesaro e Urbino"), ("PV", "Pavia"), ("PZ", "Potenza"), ("RA", "Ravenna"),
    ("RC", "Reggio Calabria"), ("RE", "Reggio Emilia"), ("RG", "Ragusa"),
    ("RI", "Rieti"), ("RM", "Roma"), ("RN", "Rimini"), ("RO", "Rovigo"),
    ("SA", "Salerno"), ("SI", "Siena"), ("SO", "Sondrio"), ("SP", "La Spezia"),
    ("SR", "Siracusa"), ("SS", "Sassari"), ("SU", "Sud Sardegna"), ("SV", "Savona"),
    ("TA", "Taranto"), ("TE", "Teramo"), ("TN", "Trento"), ("TO", "Torino"),
    ("TP", "Trapani"), ("TR", "Terni"), ("TS", "Trieste"), ("TV", "Treviso"),
    ("UD", "Udine"), ("VA", "Varese"), ("VB", "Verbano-Cusio-Ossola"),
    ("VC", "Vercelli"), ("VE", "Venezia"), ("VI", "Vicenza"), ("VR", "Verona"),
    ("VT", "Viterbo"), ("VV", "Vibo Valentia"),
)

_JP_PREFECTURES = (
    ("01", "Hokkaido"), ("02", "Aomori"), ("03", "Iwate"), ("04", "Miyagi"),
    ("05", "Akita"), ("06", "Yamagata"), ("07", "Fukushima"), ("08", "Ibaraki"),
    ("09", "Tochigi"), ("10", "Gunma"), ("11", "Saitama"), ("12", "Chiba"),
    ("13", "Tokyo"), ("14", "Kanagawa"), ("15", "Niigata"), ("16", "Toyama"),
    ("17", "Ishikawa"), ("18", "Fukui"), ("19", "Yamanashi"), ("20", "Nagano"),
    ("21", "Gifu"), ("22", "Shizuoka"), ("23", "Aichi"), ("24", "Mie"),
    ("25", "Shiga"), ("26", "Kyoto"), ("27", "Osaka"), ("28", "Hyogo"),
    ("29", "Nara"), ("30", "Wakayama"), ("31", "Tottori"), ("32", "Shimane"),
    ("33", "Okayama"), ("34", "Hiroshima"), ("35", "Yamaguchi"), ("36", "Tokushima"),
    ("37", "Kagawa"), ("38", "Ehime"), ("39", "Kochi"), ("40", "Fukuoka"),
    ("41", "Saga"), ("42", "Nagasaki"), ("43", "Kumamoto"), ("44", "Oita"),
    ("45", "Miyazaki"), ("46", "Kagoshima"), ("47", "Okinawa"),
)

_AR_PROVINCES = (
    ("B", "Buenos Aires"), ("C", "Ciudad Autónoma de Buenos Aires"),
    ("K", "Catamarca"), ("H", "Chaco"), ("U", "Chubut"),
    ("X", "Córdoba"), ("W", "Corrientes"), ("E", "Entre Ríos"),
    ("P", "Formosa"), ("Y", "Jujuy"), ("L", "La Pampa"),
    ("F", "La Rioja"), ("M", "Mendoza"), ("N", "Misiones"),
    ("Q", "Neuquén"), ("R", "Río Negro"), ("A", "Salta"),
    ("J", "San Juan"), ("D", "San Luis"), ("Z", "Santa Cruz"),
    ("S", "Santa Fe"), ("G", "Santiago del Estero"),
    ("V", "Tierra del Fuego"), ("T", "Tucumán"),
)

_IN_STATES = (
    ("AN", "Andaman and Nicobar Islands"), ("AP", "Andhra Pradesh"),
    ("AR", "Arunachal Pradesh"), ("AS", "Assam"), ("BR", "Bihar"),
    ("CH", "Chandigarh"), ("CG", "Chhattisgarh"),
    ("DN", "Dadra and Nagar Haveli and Daman and Diu"),
    ("DL", "Delhi"), ("GA", "Goa"), ("GJ", "Gujarat"), ("HR", "Haryana"),
    ("HP", "Himachal Pradesh"), ("JK", "Jammu and Kashmir"), ("JH", "Jharkhand"),
    ("KA", "Karnataka"), ("KL", "Kerala"), ("LA", "Ladakh"), ("LD", "Lakshadweep"),
    ("MP", "Madhya Pradesh"), ("MH", "Maharashtra"), ("MN", "Manipur"),
    ("ML", "Meghalaya"), ("MZ", "Mizoram"), ("NL", "Nagaland"), ("OR", "Odisha"),
    ("PY", "Puducherry"), ("PB", "Punjab"), ("RJ", "Rajasthan"), ("SK", "Sikkim"),
    ("TN", "Tamil Nadu"), ("TS", "Telangana"), ("TR", "Tripura"),
    ("UP", "Uttar Pradesh"), ("UK", "Uttarakhand"), ("WB", "West Bengal"),
)

_MY_STATES = (
    ("JHR", "Johor"), ("KDH", "Kedah"), ("KTN", "Kelantan"), ("MLK", "Melaka"),
    ("NSN", "Negeri Sembilan"), ("PHG", "Pahang"), ("PNG", "Pulau Pinang"),
    ("PRK", "Perak"), ("PLS", "Perlis"), ("SGR", "Selangor"),
    ("SBH", "Sabah"), ("SWK", "Sarawak"), ("TRG", "Terengganu"),
    ("KUL", "Kuala Lumpur"), ("LBN", "Labuan"), ("PJY", "Putrajaya"),
)

_TH_PROVINCES = (
    ("10", "Bangkok"), ("11", "Samut Prakan"), ("12", "Nonthaburi"),
    ("13", "Pathum Thani"), ("14", "Phra Nakhon Si Ayutthaya"), ("15", "Ang Thong"),
    ("16", "Lop Buri"), ("17", "Sing Buri"), ("18", "Chai Nat"), ("19", "Saraburi"),
    ("20", "Chon Buri"), ("21", "Rayong"), ("22", "Chanthaburi"), ("23", "Trat"),
    ("24", "Chachoengsao"), ("25", "Prachin Buri"), ("26", "Nakhon Nayok"),
    ("27", "Sa Kaeo"), ("30", "Nakhon Ratchasima"), ("31", "Buri Ram"),
    ("32", "Surin"), ("33", "Si Sa Ket"), ("34", "Ubon Ratchathani"),
    ("35", "Yasothon"), ("36", "Chaiyaphum"), ("37", "Amnat Charoen"),
    ("38", "Bueng Kan"), ("39", "Nong Bua Lam Phu"), ("40", "Khon Kaen"),
    ("41", "Udon Thani"), ("42", "Loei"), ("43", "Nong Khai"),
    ("44", "Maha Sarakham"), ("45", "Roi Et"), ("46", "Kalasin"),
    ("47", "Sakon Nakhon"), ("48", "Nakhon Phanom"), ("49", "Mukdahan"),
    ("50", "Chiang Mai"), ("51", "Lamphun"), ("52", "Lampang"),
    ("53", "Uttaradit"), ("54", "Phrae"), ("55", "Nan"), ("56", "Phayao"),
    ("57", "Chiang Rai"), ("58", "Mae Hong Son"), ("60", "Nakhon Sawan"),
    ("61", "Uthai Thani"), ("62", "Kamphaeng Phet"), ("63", "Tak"),
    ("64", "Sukhothai"), ("65", "Phitsanulok"), ("66", "Phichit"),
    ("67", "Phetchabun"), ("70", "Ratchaburi"), ("71", "Kanchanaburi"),
    ("72", "Suphan Buri"), ("73", "Nakhon Pathom"), ("74", "Samut Sakhon"),
    ("75", "Samut Songkhram"), ("76", "Phetchaburi"), ("77", "Prachuap Khiri Khan"),
    ("80", "Nakhon Si Thammarat"), ("81", "Krabi"), ("82", "Phangnga"),
    ("83", "Phuket"), ("84", "Surat Thani"), ("85", "Ranong"), ("86", "Chumphon"),
    ("90", "Songkhla"), ("91", "Satun"), ("92", "Trang"), ("93", "Phatthalung"),
    ("94", "Pattani"), ("95", "Yala"), ("96", "Narathiwat"),
)


# ---------------------------------------------------------------------------
# Country metadata dictionary
# ---------------------------------------------------------------------------

DEFAULT_META = CountryAddressMeta(
    country="",
    subdivision_mode="text",
    subdivisions=(),
    subdivision_label=_("State / Province"),
    postal_label=_("Postal code"),
    postal_pattern=None,
    postal_example="",
)

COUNTRY_ADDRESS_META: dict[str, CountryAddressMeta] = {

    # ── Select mode countries (subdivision dropdown required) ────────────────

    "US": CountryAddressMeta(
        country="US",
        subdivision_mode="select",
        subdivisions=_US_STATES,
        subdivision_label=_("State"),
        postal_label=_("ZIP code"),
        postal_pattern=r"^\d{5}(-\d{4})?$",
        postal_example="10001",
    ),
    "CA": CountryAddressMeta(
        country="CA",
        subdivision_mode="select",
        subdivisions=_CA_PROVINCES,
        subdivision_label=_("Province"),
        postal_label=_("Postal code"),
        postal_pattern=r"^[A-Z]\d[A-Z] \d[A-Z]\d$",
        postal_example="K1A 0A9",
    ),
    "AU": CountryAddressMeta(
        country="AU",
        subdivision_mode="select",
        subdivisions=_AU_STATES,
        subdivision_label=_("State / Territory"),
        postal_label=_("Postcode"),
        postal_pattern=r"^\d{4}$",
        postal_example="2000",
    ),
    "MX": CountryAddressMeta(
        country="MX",
        subdivision_mode="select",
        subdivisions=_MX_STATES,
        subdivision_label=_("State"),
        postal_label=_("Código postal"),
        postal_pattern=r"^\d{5}$",
        postal_example="06600",
    ),
    "BR": CountryAddressMeta(
        country="BR",
        subdivision_mode="select",
        subdivisions=_BR_STATES,
        subdivision_label=_("State"),
        postal_label=_("CEP"),
        postal_pattern=r"^\d{5}-\d{3}$",
        postal_example="01310-100",
    ),
    "ES": CountryAddressMeta(
        country="ES",
        subdivision_mode="select",
        subdivisions=_ES_COMMUNITIES,
        subdivision_label=_("Autonomous community"),
        postal_label=_("Código postal"),
        postal_pattern=r"^\d{5}$",
        postal_example="28001",
    ),
    "IT": CountryAddressMeta(
        country="IT",
        subdivision_mode="select",
        subdivisions=_IT_PROVINCES,
        subdivision_label=_("Province"),
        postal_label=_("CAP"),
        postal_pattern=r"^\d{5}$",
        postal_example="00100",
    ),
    "JP": CountryAddressMeta(
        country="JP",
        subdivision_mode="select",
        subdivisions=_JP_PREFECTURES,
        subdivision_label=_("Prefecture"),
        postal_label=_("Postal code"),
        postal_pattern=r"^\d{3}-\d{4}$",
        postal_example="100-0001",
    ),
    "AR": CountryAddressMeta(
        country="AR",
        subdivision_mode="select",
        subdivisions=_AR_PROVINCES,
        subdivision_label=_("Province"),
        postal_label=_("Código postal"),
        postal_pattern=r"^([A-Z]\d{4}[A-Z]{3}|\d{4})$",
        postal_example="C1000AAB",
    ),
    "IN": CountryAddressMeta(
        country="IN",
        subdivision_mode="select",
        subdivisions=_IN_STATES,
        subdivision_label=_("State"),
        postal_label=_("PIN code"),
        postal_pattern=r"^\d{6}$",
        postal_example="110001",
    ),
    "MY": CountryAddressMeta(
        country="MY",
        subdivision_mode="select",
        subdivisions=_MY_STATES,
        subdivision_label=_("State"),
        postal_label=_("Postcode"),
        postal_pattern=r"^\d{5}$",
        postal_example="50000",
    ),
    "TH": CountryAddressMeta(
        country="TH",
        subdivision_mode="select",
        subdivisions=_TH_PROVINCES,
        subdivision_label=_("Province"),
        postal_label=_("Postal code"),
        postal_pattern=r"^\d{5}$",
        postal_example="10100",
    ),

    # ── Text mode countries (free-text subdivision, postal pattern where applicable) ──

    "GB": CountryAddressMeta(
        country="GB",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("County"),
        postal_label=_("Postcode"),
        # Pattern applied after normalize_postal_code inserts single space before last 3.
        postal_pattern=r"^[A-Z]{1,2}\d[A-Z\d]? \d[A-Z]{2}$",
        postal_example="SW1A 1AA",
    ),
    "FR": CountryAddressMeta(
        country="FR",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Region"),
        postal_label=_("Code postal"),
        postal_pattern=r"^\d{5}$",
        postal_example="75001",
    ),
    "DE": CountryAddressMeta(
        country="DE",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("State"),
        postal_label=_("Postleitzahl"),
        postal_pattern=r"^\d{5}$",
        postal_example="10115",
    ),
    "NL": CountryAddressMeta(
        country="NL",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Province"),
        postal_label=_("Postcode"),
        # Flexible: accepts with or without space (e.g. "1234AB" or "1234 AB")
        postal_pattern=r"^\d{4}\s?[A-Z]{2}$",
        postal_example="1234 AB",
    ),
    "BE": CountryAddressMeta(
        country="BE",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Province"),
        postal_label=_("Postcode"),
        postal_pattern=r"^\d{4}$",
        postal_example="1000",
    ),
    "AT": CountryAddressMeta(
        country="AT",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("State"),
        postal_label=_("Postleitzahl"),
        postal_pattern=r"^\d{4}$",
        postal_example="1010",
    ),
    "CH": CountryAddressMeta(
        country="CH",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Canton"),
        postal_label=_("NPA"),
        postal_pattern=r"^\d{4}$",
        postal_example="8001",
    ),
    "IE": CountryAddressMeta(
        country="IE",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("County"),
        postal_label=_("Eircode"),
        # Flexible: accepts "D02XY45" or "D02 XY45"
        postal_pattern=r"^[A-Z\d]{3}\s?[A-Z\d]{4}$",
        postal_example="D02 XY45",
    ),
    "PT": CountryAddressMeta(
        country="PT",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("District"),
        postal_label=_("Código postal"),
        # After normalize_postal_code inserts hyphen: "NNNN-NNN"
        postal_pattern=r"^\d{4}-\d{3}$",
        postal_example="1000-001",
    ),
    "SE": CountryAddressMeta(
        country="SE",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("County"),
        postal_label=_("Postnummer"),
        # Flexible: accepts "12345" or "123 45"
        postal_pattern=r"^\d{3}\s?\d{2}$",
        postal_example="123 45",
    ),
    "DK": CountryAddressMeta(
        country="DK",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Region"),
        postal_label=_("Postnummer"),
        postal_pattern=r"^\d{4}$",
        postal_example="1000",
    ),
    "NO": CountryAddressMeta(
        country="NO",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("County"),
        postal_label=_("Postnummer"),
        postal_pattern=r"^\d{4}$",
        postal_example="0150",
    ),
    "FI": CountryAddressMeta(
        country="FI",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Region"),
        postal_label=_("Postinumero"),
        postal_pattern=r"^\d{5}$",
        postal_example="00100",
    ),
    "PL": CountryAddressMeta(
        country="PL",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Province"),
        postal_label=_("Kod pocztowy"),
        # After normalize_postal_code inserts hyphen: "NN-NNN"
        postal_pattern=r"^\d{2}-\d{3}$",
        postal_example="00-001",
    ),
    "CZ": CountryAddressMeta(
        country="CZ",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Region"),
        postal_label=_("PSČ"),
        # Flexible: accepts "12345" or "123 45"
        postal_pattern=r"^\d{3}\s?\d{2}$",
        postal_example="110 00",
    ),
    "NZ": CountryAddressMeta(
        country="NZ",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Region"),
        postal_label=_("Postcode"),
        postal_pattern=r"^\d{4}$",
        postal_example="6011",
    ),
    "KR": CountryAddressMeta(
        country="KR",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Province / City"),
        postal_label=_("Postal code"),
        postal_pattern=r"^\d{5}$",
        postal_example="03000",
    ),
    "ZA": CountryAddressMeta(
        country="ZA",
        subdivision_mode="text",
        subdivisions=(),
        subdivision_label=_("Province"),
        postal_label=_("Postal code"),
        postal_pattern=r"^\d{4}$",
        postal_example="2000",
    ),

    # ── None mode countries (no subdivision; field hidden) ───────────────────

    "SG": CountryAddressMeta(
        country="SG",
        subdivision_mode="none",
        subdivisions=(),
        subdivision_label=_("District"),
        postal_label=_("Postal code"),
        postal_pattern=r"^\d{6}$",
        postal_example="018989",
    ),
    "HK": CountryAddressMeta(
        country="HK",
        subdivision_mode="none",
        subdivisions=(),
        subdivision_label=_("District"),
        postal_label=_("Postal code"),
        postal_pattern=None,
        postal_example="",
    ),
}


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def get_country_meta(country_code: str) -> CountryAddressMeta:
    """
    Return the CountryAddressMeta for country_code, or DEFAULT_META if unknown.

    Never raises; never returns None. Unknown or empty input → DEFAULT_META
    (exactly today's v1 form behaviour — §XV-3 explicit degradation).

    Input is uppercased and stripped before lookup.
    """
    if not country_code:
        return DEFAULT_META
    return COUNTRY_ADDRESS_META.get(country_code.strip().upper(), DEFAULT_META)


def normalize_postal_code(country: str, raw: str) -> str:
    """
    Normalize a postal code for the given country.

    Base: strip, uppercase, collapse internal whitespace.
    Per-country canonicalization:
      GB — insert single space before last 3 chars (e.g. "SW1A1AA" → "SW1A 1AA")
      CA — insert space after position 3 for 6-char codes (e.g. "K1A0A9" → "K1A 0A9")
      JP — insert hyphen after 3 digits when 7 bare digits (e.g. "1000001" → "100-0001")
      BR — insert hyphen after 5 digits when 8 bare digits (e.g. "01310100" → "01310-100")
      PT — insert hyphen after 4 digits when 7 bare digits (e.g. "1000001" → "1000-001")
      PL — insert hyphen after 2 digits when 5 bare digits (e.g. "00001" → "00-001")

    Returns the normalized value to both store and validate against postal_pattern.
    """
    country = country.strip().upper()
    # Base: strip, uppercase, collapse internal whitespace to single space
    s = re.sub(r'\s+', ' ', raw.strip()).upper()

    if country == 'GB':
        # Remove all spaces, then insert one space before the last 3 chars.
        s_no_space = s.replace(' ', '')
        if len(s_no_space) >= 4:
            s = s_no_space[:-3] + ' ' + s_no_space[-3:]
        else:
            s = s_no_space

    elif country == 'CA':
        # A1A 1A1 format: ensure space after position 3 for 6-alphanumeric codes.
        s_no_space = s.replace(' ', '')
        if len(s_no_space) == 6:
            s = s_no_space[:3] + ' ' + s_no_space[3:]
        else:
            s = s_no_space

    elif country == 'JP':
        # 123-4567 format: insert hyphen after pos 3 when 7 bare digits.
        s_clean = s.replace('-', '').replace(' ', '')
        if len(s_clean) == 7 and s_clean.isdigit():
            s = s_clean[:3] + '-' + s_clean[3:]
        else:
            s = s.replace(' ', '')

    elif country == 'BR':
        # NNNNN-NNN format: insert hyphen when 8 bare digits.
        s_clean = s.replace('-', '').replace(' ', '')
        if len(s_clean) == 8 and s_clean.isdigit():
            s = s_clean[:5] + '-' + s_clean[5:]
        else:
            s = s.replace(' ', '')

    elif country == 'PT':
        # NNNN-NNN format: insert hyphen when 7 bare digits.
        s_clean = s.replace('-', '').replace(' ', '')
        if len(s_clean) == 7 and s_clean.isdigit():
            s = s_clean[:4] + '-' + s_clean[4:]
        else:
            s = s.replace(' ', '')

    elif country == 'PL':
        # NN-NNN format: insert hyphen when 5 bare digits.
        s_clean = s.replace('-', '').replace(' ', '')
        if len(s_clean) == 5 and s_clean.isdigit():
            s = s_clean[:2] + '-' + s_clean[2:]
        else:
            s = s.replace(' ', '')

    return s


def _nfd_lower(text: str) -> str:
    """
    NFD-normalize then lowercase and strip for accent-insensitive comparison.

    Shared with core/slugs.py approach: NFD decomposes accented chars so that
    e.g. 'é' and 'e' compare equal after the ASCII encode/ignore step.
    Used by normalize_subdivision to match subdivision labels regardless of
    diacritics (e.g. "yucatan" matches "Yucatán").
    """
    return (
        unicodedata.normalize("NFD", text)
        .encode("ascii", "ignore")
        .decode("ascii")
        .lower()
        .strip()
    )


def normalize_subdivision(meta: CountryAddressMeta, raw: str) -> str | None:
    """
    Normalize a subdivision value for 'select' mode countries.

    Accepts:
      - Bare code, case-insensitive: "ny" → "NY"
      - Label, case- and accent-insensitive via NFD: "new york" → "NY"
        e.g. MX "yucatan" matches "Yucatán" → "YU"

    Returns the canonical bare code on success, or None when no match.
    Caller must add the form error for None returns.

    For non-'select' mode, this function should not be called (clean() handles
    those modes directly), but if called returns raw.strip() or None if empty.
    """
    if meta.subdivision_mode != 'select':
        stripped = raw.strip()
        return stripped if stripped else None

    raw_stripped = raw.strip()
    if not raw_stripped:
        return None

    raw_upper = raw_stripped.upper()
    raw_nfd = _nfd_lower(raw_stripped)

    for code, label in meta.subdivisions:
        # Case-insensitive code match
        if code.upper() == raw_upper:
            return code
        # Accent- and case-insensitive label match
        if _nfd_lower(label) == raw_nfd:
            return code

    return None
