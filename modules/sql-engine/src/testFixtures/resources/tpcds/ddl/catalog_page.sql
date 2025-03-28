create table CATALOG_PAGE
(
    CP_CATALOG_PAGE_SK     INTEGER not null,
    CP_CATALOG_PAGE_ID     VARCHAR(16),
    CP_START_DATE_SK       INTEGER,
    CP_END_DATE_SK         INTEGER,
    CP_DEPARTMENT          VARCHAR(50),
    CP_CATALOG_NUMBER      INTEGER,
    CP_CATALOG_PAGE_NUMBER INTEGER,
    CP_DESCRIPTION         VARCHAR(100),
    CP_TYPE                VARCHAR(100),
    constraint CATALOG_PAGE_PK
        primary key (CP_CATALOG_PAGE_SK)
);
