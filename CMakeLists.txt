cmake_minimum_required(VERSION 3.5)

project(WebsiteEmpire2)

enable_testing()

add_subdirectory(WebsiteEmpireLib)
add_subdirectory(WebsiteAspire)
add_subdirectory(WebsiteEmpire)
add_subdirectory(WebsiteAspireTests)
add_subdirectory(WebsiteEmpireTests)
add_subdirectory(StaticWebsiteServeLib)
add_subdirectory(StaticWebsiteServe)
add_subdirectory(StaticWebsiteServeTests)

add_dependencies(WebsiteAspire
    WebsiteEmpireLib
)
add_dependencies(WebsiteEmpire
    WebsiteEmpireLib
)
add_dependencies(Test_Aspire_Page_Attributes
    WebsiteEmpireLib
)
add_dependencies(Test_Website_PageBlocText
    WebsiteEmpireLib
)
add_dependencies(Test_Website_PageTypeRegistry
    WebsiteEmpireLib
)
add_dependencies(Test_Website_PageDb
    WebsiteEmpireLib
)
add_dependencies(Test_Website_PageRepository
    WebsiteEmpireLib
)
add_dependencies(Test_Website_PageGenerator
    WebsiteEmpireLib
)
add_dependencies(Test_StaticServe_VariantSelector
    StaticWebsiteServeLib
)
add_dependencies(StaticWebsiteServe
    StaticWebsiteServeLib
)
add_dependencies(Test_StaticServe_Db
    StaticWebsiteServeLib
)
