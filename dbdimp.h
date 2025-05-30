/*
 *  DBD::MariaDB - DBI driver for the MariaDB and MySQL database
 *
 *  Copyright (c) 2005       Patrick Galbraith
 *  Copyright (c) 2003       Rudolf Lippan
 *  Copyright (c) 1997-2003  Jochen Wiedmann
 *
 *  Based on DBD::Oracle; DBD::Oracle is
 *
 *  Copyright (c) 1994,1995  Tim Bunce
 *
 *  You may distribute this under the terms of either the GNU General Public
 *  License or the Artistic License, as specified in the Perl README file.
 */

#define PERL_NO_GET_CONTEXT
/*
 *  Header files we use
 */

/*
 * On WIN32 windows.h and winsock.h need to be included before mysql.h
 * Otherwise SOCKET type which is needed for mysql.h is not defined
 * SO_UPDATE_CONNECT_CONTEXT is not defined in all MinGW versions.
 */
#ifdef _WIN32
#include <windows.h>
#include <winsock.h>
#ifndef SO_UPDATE_CONNECT_CONTEXT
#define SO_UPDATE_CONNECT_CONTEXT 0x7010
#endif
#endif

#include <mysql.h>  /* Comes with MySQL-devel */
#include <mysqld_error.h>  /* Comes MySQL */
#include <errmsg.h> /* Comes with MySQL-devel */

#ifndef MYSQL_VERSION_ID
#include <mysql_version.h> /* Comes with MySQL-devel */
#endif

#include <DBIXS.h>  /* installed by the DBI module */
#include <stdint.h> /* For uint32_t */


/*******************************************************************************
 * Standard MariaDB macros which are not defined in every MySQL/MariaDB client *
 *******************************************************************************/

#if !defined(MARIADB_BASE_VERSION) && defined(MARIADB_PACKAGE_VERSION)
#define MARIADB_BASE_VERSION
#endif

/* Macro is available in my_global.h which is not included or present in some versions of MariaDB */
#ifndef NOT_FIXED_DEC
#define NOT_FIXED_DEC 31
#endif

/* Macro is available in m_ctype.h which is not included in some versions of MySQL */
#ifndef MY_CS_PRIMARY
#define MY_CS_PRIMARY 32
#endif

/* Macro is available in mysql_com.h, but not defined in older MySQL versions */
#ifndef SERVER_STATUS_NO_BACKSLASH_ESCAPES
#define SERVER_STATUS_NO_BACKSLASH_ESCAPES 512
#endif

/* Macro is not defined in older MySQL versions */
#ifndef CR_NO_STMT_METADATA
#define CR_NO_STMT_METADATA 2052
#endif

/* Macro is not defined in some MariaDB versions */
#ifndef CR_NO_RESULT_SET
#define CR_NO_RESULT_SET 2053
#endif

/* Macro is not defined in older MySQL versions */
#ifndef CR_NOT_IMPLEMENTED
#define CR_NOT_IMPLEMENTED 2054
#endif

/* Macro is not defined in older MySQL versions */
#ifndef CR_STMT_CLOSED
#define CR_STMT_CLOSED 2056
#endif

/* Macro was added in MySQL 8.0.24 */
#ifndef ER_CLIENT_INTERACTION_TIMEOUT
#define ER_CLIENT_INTERACTION_TIMEOUT 4031
#endif


/********************************************************************
 * Standard Perl macros which are not defined in every Perl version *
 ********************************************************************/

#ifndef PERL_STATIC_INLINE
#define PERL_STATIC_INLINE static
#endif

#ifndef NOT_REACHED
#define NOT_REACHED assert(0)
#endif

#ifndef SVfARG
#define SVfARG(p) ((void*)(p))
#endif

#ifndef SSize_t_MAX
#define SSize_t_MAX (SSize_t)(~(Size_t)0 >> 1)
#endif

/* _set_osfhnd() is copied from Perl source file win32.h */
#ifdef _WIN32
typedef intptr_t ioinfo;
extern __declspec(dllimport) ioinfo* __pioinfo[];
#  define IOINFO_L2E 5
#  define IOINFO_ARRAY_ELTS (1 << IOINFO_L2E)
#  define _ioinfo_size (_msize((void*)__pioinfo[0]) / IOINFO_ARRAY_ELTS)
#  define _pioinfo(i) ((intptr_t *) \
    (((Size_t)__pioinfo[(i) >> IOINFO_L2E])/* * to head of array ioinfo [] */\
     /* offset to the head of a particular ioinfo struct */ \
     + (((i) & (IOINFO_ARRAY_ELTS - 1)) * _ioinfo_size)) \
   )
#  define _osfhnd(i) (*(_pioinfo(i)))
#  define _set_osfhnd(fh, osfh) (void)(_osfhnd(fh) = (intptr_t)osfh)
#endif

/* PERL_UNUSED_ARG does not exist prior to perl 5.9.3 */
#ifndef PERL_UNUSED_ARG
#  if defined(lint) && defined(S_SPLINT_S) /* www.splint.org */
#    include <note.h>
#    define PERL_UNUSED_ARG(x) NOTE(ARGUNUSED(x))
#  else
#    define PERL_UNUSED_ARG(x) ((void)x)
#  endif
#endif

/* assert_not_ROK is broken prior to perl 5.8.2 */
#if PERL_VERSION < 8 || (PERL_VERSION == 8 && PERL_SUBVERSION < 2)
#undef assert_not_ROK
#define assert_not_ROK(sv)
#endif

#ifndef SvPV_nomg_nolen
#define SvPV_nomg_nolen(sv) ((SvFLAGS(sv) & (SVf_POK)) == SVf_POK ? SvPVX(sv) : sv_2pv_flags(sv, &PL_na, 0))
#endif

/* Remove wrong SV_NOSTEAL macro defined by ppport.h */
#if defined(SV_NOSTEAL) && (SV_NOSTEAL == 0)
#undef SV_NOSTEAL
#endif

#ifndef newSVsv_nomg
PERL_STATIC_INLINE SV *newSVsv_nomg(pTHX_ SV *sv)
{
  SV *ret = newSV(0);
#ifndef SV_NOSTEAL
  U32 tmp = SvFLAGS(sv) & SVs_TEMP;
  SvTEMP_off(sv);
  sv_setsv_flags(ret, sv, 0);
  SvFLAGS(sv) |= tmp;
#else
  sv_setsv_flags(ret, sv, SV_NOSTEAL);
#endif
  return ret;
}
#define newSVsv_nomg(sv) newSVsv_nomg(aTHX_ (sv))
#endif

/* looks_like_number() process get magic prior to perl 5.15.4, so reimplement it */
#if PERL_VERSION < 15 || (PERL_VERSION == 15 && PERL_SUBVERSION < 4)
#undef looks_like_number
PERL_STATIC_INLINE I32 looks_like_number(pTHX_ SV *sv)
{
  char *sbegin;
  STRLEN len;
  if (!SvPOK(sv) && !SvPOKp(sv))
    return SvFLAGS(sv) & (SVf_NOK|SVp_NOK|SVf_IOK|SVp_IOK);
  sbegin = SvPV_nomg(sv, len);
  return grok_number(sbegin, len, NULL);
}
#define looks_like_number(sv) looks_like_number(aTHX_ (sv))
#endif

#ifndef SvPVutf8_nomg
PERL_STATIC_INLINE char * SvPVutf8_nomg(pTHX_ SV *sv, STRLEN *len)
{
  char *buf = SvPV_nomg(sv, *len);
  if (SvUTF8(sv))
    return buf;
  if (SvGMAGICAL(sv))
    sv = sv_2mortal(newSVpvn(buf, *len));
  /* There is sv_utf8_upgrade_nomg(), but it is broken prior to Perl version 5.13.10 */
  return SvPVutf8(sv, *len);
}
#define SvPVutf8_nomg(sv, len) SvPVutf8_nomg(aTHX_ (sv), &(len))
#endif

#ifndef SvPVbyte_nomg
PERL_STATIC_INLINE char * SvPVbyte_nomg(pTHX_ SV *sv, STRLEN *len)
{
  char *buf = SvPV_nomg(sv, *len);
  if (!SvUTF8(sv))
    return buf;
  if (SvGMAGICAL(sv))
  {
    sv = sv_2mortal(newSVpvn(buf, *len));
    SvUTF8_on(sv);
  }
  return SvPVbyte(sv, *len);
}
#define SvPVbyte_nomg(sv, len) SvPVbyte_nomg(aTHX_ (sv), &(len))
#endif

#ifndef SvTRUE_nomg
#define SvTRUE_nomg(sv) (!SvGMAGICAL((sv)) ? SvTRUE((sv)) : SvTRUEx(sv_2mortal(newSVsv_nomg((sv)))))
#endif

/* Remove wrong SvIV_nomg macro defined by ppport.h */
#if defined(SvIV_nomg) && (PERL_VERSION < 9 | (PERL_VERSION == 9 && PERL_SUBVERSION < 1))
#undef SvIV_nomg
#endif

#ifndef SvIV_nomg
PERL_STATIC_INLINE IV SvIV_nomg(pTHX_ SV *sv)
{
  IV iv;
  SV *tmp;
  if (!SvGMAGICAL(sv))
    return SvIV(sv);
  tmp = sv_2mortal(newSVsv_nomg(sv));
  iv = SvIV(tmp);
  if (SvIsUV(tmp))
    SvIsUV_on(sv);
  else
    SvIsUV_off(sv);
  return iv;
}
#define SvIV_nomg(sv) SvIV_nomg(aTHX_ (sv))
#endif

/* Remove wrong SvUV_nomg macro defined by ppport.h */
#if defined(SvUV_nomg) && (PERL_VERSION < 9 | (PERL_VERSION == 9 && PERL_SUBVERSION < 1))
#undef SvUV_nomg
#endif

#ifndef SvUV_nomg
PERL_STATIC_INLINE UV SvUV_nomg(pTHX_ SV *sv)
{
  UV uv;
  SV *tmp;
  if (!SvGMAGICAL(sv))
    return SvUV(sv);
  tmp = sv_2mortal(newSVsv_nomg(sv));
  uv = SvUV(tmp);
  if (SvIsUV(tmp))
    SvIsUV_on(sv);
  else
    SvIsUV_off(sv);
  return uv;
}
#define SvUV_nomg(sv) SvUV_nomg(aTHX_ (sv))
#endif

#ifndef SvNV_nomg
#define SvNV_nomg(sv) (!SvGMAGICAL((sv)) ? SvNV((sv)) : SvNVx(sv_2mortal(newSVsv_nomg((sv)))))
#endif

#ifndef sv_cmp_flags
#define sv_cmp_flags(sv1, sv2, flags) (((flags) & SV_GMAGIC) ? sv_cmp((sv1), (sv2)) : sv_cmp((!SvGMAGICAL((sv1)) ? (sv1) : sv_2mortal(newSVsv_nomg((sv1)))), (!SvGMAGICAL((sv2)) ? (sv2) : sv_2mortal(newSVsv_nomg((sv2))))))
#endif

#ifndef gv_stashpvs
#define gv_stashpvs(str, flags) gv_stashpvn("" str "", sizeof((str))-1, (flags))
#endif

#ifndef newSVpvs
#define newSVpvs(str) newSVpvn("" str "", sizeof((str)) - 1)
#endif

#ifndef hv_fetchs
#define hv_fetchs(hv, key, lval) hv_fetch((hv), "" key "", sizeof((key))-1, (lval))
#endif

#ifndef hv_stores
#define hv_stores(hv, key, val) hv_store((hv), "" key "", sizeof((key))-1, (val), 0)
#endif

#ifndef hv_deletes
#define hv_deletes(hv, key, flags) hv_delete((hv), "" key "", sizeof((key))-1, (flags))
#endif

#ifndef memEQs
#define memEQs(s1, l, s2) (sizeof((s2))-1 == (l) && memEQ((s1), "" s2 "", sizeof((s2))-1))
#endif

#ifndef strBEGINs
#define strBEGINs(s1, s2) strnEQ((s1), "" s2 "", sizeof((s2))-1)
#endif


/**************************************
 * Custom DBD-MariaDB specific macros *
 **************************************/

#define GEO_DATATYPE_VERSION 50007
#define NEW_DATATYPE_VERSION 50003

#if MYSQL_VERSION_ID < NEW_DATATYPE_VERSION
#define MYSQL_TYPE_VARCHAR 15
#define MYSQL_TYPE_BIT 16
#define MYSQL_TYPE_NEWDECIMAL 246
#endif

#if MYSQL_VERSION_ID < GEO_DATATYPE_VERSION
#define MYSQL_TYPE_GEOMETRY 255
#endif

/*
 * This is the versions of libmysql that supports MySQL Fabric.
 * We need to check for special macro LIBMYSQL_VERSION_ID.
 */
#ifdef LIBMYSQL_VERSION_ID
#if LIBMYSQL_VERSION_ID >= 60200 && LIBMYSQL_VERSION_ID < 70000
#define HAVE_FABRIC
#endif
#endif

#if !defined(MARIADB_BASE_VERSION) && MYSQL_VERSION_ID >= 80001
#define my_bool bool
#endif

/*
 * MariaDB Connector/C 3.1.10 changed API of mysql_get_client_version()
 * function. Before that release it returned client version. With that release
 * it started returning Connector/C package version.
 *
 * So when compiling with MariaDB Connector/C client library, redefine
 * mysql_get_client_version() to always returns client version via function
 * mariadb_get_infov(MARIADB_CLIENT_VERSION_ID) call.
 *
 * Driver code expects for a long time that mysql_get_client_version() call
 * returns client version and not something different.
 *
 * Function mariadb_get_infov() is supported since MariaDB Connector/C 3.0+.
 */
#if defined(MARIADB_PACKAGE_VERSION) && defined(MARIADB_PACKAGE_VERSION_ID) && MARIADB_PACKAGE_VERSION_ID >= 30000
PERL_STATIC_INLINE unsigned long mariadb_get_client_version(void)
{
  /* MARIADB_CLIENT_VERSION_ID really expects size_t type, documentation is wrong and says unsigned int. */
  size_t version;
  if (mariadb_get_infov(NULL, MARIADB_CLIENT_VERSION_ID, &version) != 0)
    version = mysql_get_client_version(); /* On error fallback to mysql_get_client_version() */
  return version;
}
#define mysql_get_client_version() mariadb_get_client_version()
#endif

/* mysql_commit() and mysql_rollback() are broken in MariaDB Connector/C prior to version 3.1.3, see: https://jira.mariadb.org/browse/CONC-400 */
#if defined(MARIADB_PACKAGE_VERSION) && MARIADB_PACKAGE_VERSION_ID < 30103
#define mysql_commit(mysql) ((my_bool)(mysql_real_query((mysql), "COMMIT", 6)))
#define mysql_rollback(mysql) ((my_bool)(mysql_real_query((mysql), "ROLLBACK", 8)))
#endif

/* MYSQL_SECURE_AUTH became a no-op from MySQL 5.7.5 and is removed from MySQL 8.0.3 */
#if defined(MARIADB_BASE_VERSION) || MYSQL_VERSION_ID <= 50704
#define HAVE_SECURE_AUTH
#endif

/* mysql_error(NULL) returns last error message, needs MySQL 5.0.60+ or 5.1.24+; does not work with MariaDB Connector/C yet: https://jira.mariadb.org/browse/CONC-374 */
#if ((MYSQL_VERSION_ID >= 50060 && MYSQL_VERSION_ID < 50100) || MYSQL_VERSION_ID >= 50124) && !defined(MARIADB_PACKAGE_VERSION)
#define HAVE_LAST_ERROR
#endif

/*
 * MySQL and MariaDB Embedded are affected by https://jira.mariadb.org/browse/MDEV-16578
 * MariaDB 10.2.2+ prior to 10.2.19 and 10.3.9 and MariaDB Connector/C prior to 3.0.5 are affected by https://jira.mariadb.org/browse/CONC-336
 * MySQL 8.0.4+ prior to 8.0.20 is affected too by https://bugs.mysql.com/bug.php?id=93276
 */
#if defined(HAVE_EMBEDDED) || (!defined(MARIADB_BASE_VERSION) && MYSQL_VERSION_ID >= 80004 && MYSQL_VERSION_ID < 80020) || (defined(MARIADB_PACKAGE_VERSION) && (!defined(MARIADB_PACKAGE_VERSION_ID) || MARIADB_PACKAGE_VERSION_ID < 30005)) || (defined(MARIADB_BASE_VERSION) && ((MYSQL_VERSION_ID >= 100202 && MYSQL_VERSION_ID < 100219) || (MYSQL_VERSION_ID >= 100300 && MYSQL_VERSION_ID < 100309)))
#define HAVE_BROKEN_INIT
#endif

/*
 * Check which SSL settings are supported by API at compile time
 */

/* Use mysql_options with MYSQL_OPT_SSL_VERIFY_SERVER_CERT */
#if ((MYSQL_VERSION_ID >= 50023 && MYSQL_VERSION_ID < 50100) || MYSQL_VERSION_ID >= 50111) && (MYSQL_VERSION_ID < 80000 || defined(MARIADB_BASE_VERSION))
#define HAVE_SSL_VERIFY
#endif

/* Use mysql_options with MYSQL_OPT_SSL_ENFORCE (CVE-2015-3152, fix for MySQL) */
#if !defined(MARIADB_BASE_VERSION) && !defined(HAVE_EMBEDDED) && MYSQL_VERSION_ID >= 50703 && MYSQL_VERSION_ID < 80000 && MYSQL_VERSION_ID != 60000
#define HAVE_SSL_ENFORCE
#endif

/* Use mysql_options with MYSQL_OPT_SSL_MODE (CVE-2015-3152, fix for MySQL) */
#if !defined(MARIADB_BASE_VERSION) && !defined(HAVE_EMBEDDED) && MYSQL_VERSION_ID >= 50711 && MYSQL_VERSION_ID != 60000
#define HAVE_SSL_MODE
#endif

/* Use mysql_options with MYSQL_OPT_SSL_MODE, but only SSL_MODE_REQUIRED is supported (CVE-2017-3305, fix for MySQL) */
#if !defined(MARIADB_BASE_VERSION) && !defined(HAVE_EMBEDDED) && ((MYSQL_VERSION_ID >= 50636 && MYSQL_VERSION_ID < 50700) || (MYSQL_VERSION_ID >= 50555 && MYSQL_VERSION_ID < 50600))
#define HAVE_SSL_MODE_ONLY_REQUIRED
#endif

/*
 * Check which SSL settings are supported by API at runtime
 */

/* MYSQL_OPT_SSL_VERIFY_SERVER_CERT automatically enforce SSL mode (CVE-2015-3152 and CVE-2017-3305 and CVE-2018-2767, fix for MariaDB) */
PERL_STATIC_INLINE bool ssl_verify_also_enforce_ssl(void) {
#ifdef MARIADB_BASE_VERSION
	unsigned long version = mysql_get_client_version();
 #ifdef HAVE_EMBEDDED
	return ((version >= 50560 && version < 50600) || (version >= 100035 && version < 100100) || (version >= 100133 && version < 100200) || (version >= 100215 && version < 100300) || version >= 100307);
 #else
	return ((version >= 50556 && version < 50600) || (version >= 100031 && version < 100100) || (version >= 100123 && version < 100200) || (version >= 100206 && version < 100300) || version >= 100301);
 #endif
#else
	return FALSE;
#endif
}

/* MYSQL_OPT_SSL_VERIFY_SERVER_CERT is not vulnerable (CVE-2016-2047) and can be used */
PERL_STATIC_INLINE bool ssl_verify_usable(void) {
	unsigned long version = mysql_get_client_version();
#ifdef MARIADB_BASE_VERSION
	return ((version >= 50547 && version < 50600) || (version >= 100023 && version < 100100) || version >= 100110);
#else
	return ((version >= 50549 && version < 50600) || (version >= 50630 && version < 50700) || version >= 50712);
#endif
}

/*
 *  Internal constants, used for fetching array attributes
 */
enum av_attribs {
    AV_ATTRIB_NAME = 0,
    AV_ATTRIB_TABLE,
    AV_ATTRIB_TYPE,
    AV_ATTRIB_SQL_TYPE,
    AV_ATTRIB_IS_PRI_KEY,
    AV_ATTRIB_IS_NOT_NULL,
    AV_ATTRIB_NULLABLE,
    AV_ATTRIB_LENGTH,
    AV_ATTRIB_IS_NUM,
    AV_ATTRIB_TYPE_NAME,
    AV_ATTRIB_PRECISION,
    AV_ATTRIB_SCALE,
    AV_ATTRIB_MAX_LENGTH,
    AV_ATTRIB_IS_KEY,
    AV_ATTRIB_IS_BLOB,
    AV_ATTRIB_IS_AUTO_INCREMENT,
    AV_ATTRIB_LAST         /*  Dummy attribute, never used, for allocation  */
};                         /*  purposes only                                */


/* Double linked list */
struct mariadb_list_entry {
    void *data;
    struct mariadb_list_entry *prev;
    struct mariadb_list_entry *next;
};
#define mariadb_list_add(list, entry, ptr)           \
  STMT_START {                                       \
    Newz(0, (entry), 1, struct mariadb_list_entry);  \
    (entry)->data = (ptr);                           \
    (entry)->prev = NULL;                            \
    (entry)->next = (list);                          \
    if ((list))                                      \
      (list)->prev = (entry);                        \
    (list) = (entry);                                \
  } STMT_END
#define mariadb_list_remove(list, entry)             \
  STMT_START {                                       \
    if ((entry)->prev)                               \
      (entry)->prev->next = (entry)->next;           \
    if ((entry)->next)                               \
      (entry)->next->prev = (entry)->prev;           \
    if ((list) == (entry))                           \
      (list) = (entry)->next;                        \
    Safefree((entry));                               \
    (entry) = NULL;                                  \
  } STMT_END


/*
 *  This is our part of the driver handle. We receive the handle as
 *  an "SV*", say "drh", and receive a pointer to the structure below
 *  by declaring
 *
 *    D_imp_drh(drh);
 *
 *  This declares a variable called "imp_drh" of type
 *  "struct imp_drh_st *".
 */
struct imp_drh_st {
    dbih_drc_t com;         /* MUST be first element in structure   */

    struct mariadb_list_entry *active_imp_dbhs; /* List of imp_dbh structures with active MYSQL* */
    struct mariadb_list_entry *taken_pmysqls;   /* List of active MYSQL* from take_imp_data() */
    unsigned long int instances;
    bool non_embedded_started;
#if !defined(HAVE_EMBEDDED) && defined(HAVE_BROKEN_INIT)
    bool non_embedded_finished;
#endif
    bool embedded_started;
    SV *embedded_args;
    SV *embedded_groups;
};


/*
 *  Likewise, this is our part of the database handle, as returned
 *  by DBI->connect. We receive the handle as an "SV*", say "dbh",
 *  and receive a pointer to the structure below by declaring
 *
 *    D_imp_dbh(dbh);
 *
 *  This declares a variable called "imp_dbh" of type
 *  "struct imp_dbh_st *".
 */
struct imp_dbh_st {
    dbih_dbc_t com;         /*  MUST be first element in structure   */

    struct mariadb_list_entry *list_entry; /* Entry of imp_drh->active_imp_dbhs list */
    MYSQL *pmysql;
    int sock_fd;
    bool connected;          /* Set to true after DBI->connect finished */
    bool is_embedded;
    bool auto_reconnect;
    bool bind_type_guessing;
    bool bind_comment_placeholders;
    bool no_autocommit_cmd;
    bool use_mysql_use_result; /* TRUE if execute should use
                               * mysql_use_result rather than
                               * mysql_store_result
                               */
    bool use_server_side_prepare;
    bool disable_fallback_for_server_prepare;
    bool use_multi_statements;
    void* async_query_in_flight;
    my_ulonglong insertid;
    struct {
	    unsigned int auto_reconnects_ok;
	    unsigned int auto_reconnects_failed;
    } stats;
};


/*
 *  The bind_param method internally uses this structure for storing
 *  parameters.
 */
typedef struct imp_sth_ph_st {
    char* value;
    STRLEN len;
    int type;
    bool bound;
} imp_sth_ph_t;

/*
 *  Storage for numeric value in prepared statement
 */
typedef union numeric_val_u {
    unsigned char tval;
    unsigned short sval;
    uint32_t lval;
    my_ulonglong llval;
    float fval;
    double dval;
} numeric_val_t;

/*
 *  The bind_param method internally uses this structure for storing
 *  parameters.
 */
typedef struct imp_sth_phb_st {
    numeric_val_t   numeric_val;
    unsigned long   length;
    my_bool         is_null;
} imp_sth_phb_t;

/*
 *  The mariadb_st_describe uses this structure for storing
 *  fields meta info.
 */
typedef struct imp_sth_fbh_st {
    unsigned long  length;
    my_bool        is_null;
    my_bool        error;
    char           *data;
    numeric_val_t  numeric_val;
    bool           is_utf8;
} imp_sth_fbh_t;


typedef struct imp_sth_fbind_st {
   unsigned long   * length;
   my_bool         * is_null;
} imp_sth_fbind_t;


/*
 *  Finally our part of the statement handle. We receive the handle as
 *  an "SV*", say "dbh", and receive a pointer to the structure below
 *  by declaring
 *
 *    D_imp_sth(sth);
 *
 *  This declares a variable called "imp_sth" of type
 *  "struct imp_sth_st *".
 */
struct imp_sth_st {
    dbih_stc_t com;       /* MUST be first element in structure     */
    char *statement;
    STRLEN statement_len;

    MYSQL_STMT       *stmt;
    MYSQL_BIND       *bind;
    MYSQL_BIND       *buffer;
    imp_sth_phb_t    *fbind;
    imp_sth_fbh_t    *fbh;
    bool             has_been_bound;
    bool use_server_side_prepare;  /* server side prepare statements? */
    bool disable_fallback_for_server_prepare;

    MYSQL_RES* result;       /* result                                 */
    my_ulonglong currow;  /* number of current row                  */
    my_ulonglong row_num;         /* total number of rows                   */

    bool  done_desc;      /* have we described this sth yet ?	    */
    long  long_buflen;    /* length for long/longraw (if >0)	    */
    bool  long_trunc_ok;  /* is truncating a long an error	    */
    my_ulonglong insertid; /* ID of auto insert                      */
    unsigned int warning_count;  /* Number of warnings after execute()     */
    imp_sth_ph_t* params; /* Pointer to parameter array             */
    AV* av_attr[AV_ATTRIB_LAST];/*  For caching array attributes        */
    bool  use_mysql_use_result;  /*  TRUE if execute should use     */
                          /* mysql_use_result rather than           */
                          /* mysql_store_result */

    bool is_async;
    bool async_result;
};


/*
 *  And last, not least: The prototype definitions.
 *
 * These defines avoid name clashes for multiple statically linked DBD's	*/
#define dbd_init		mariadb_dr_init
#define dbd_discon_all		mariadb_dr_discon_all
#define dbd_take_imp_data	mariadb_db_take_imp_data
#define dbd_db_login6_sv	mariadb_db_login6_sv
#define dbd_db_do6		mariadb_db_do6
#define dbd_db_commit		mariadb_db_commit
#define dbd_db_rollback		mariadb_db_rollback
#define dbd_db_disconnect	mariadb_db_disconnect
#define dbd_db_destroy		mariadb_db_destroy
#define dbd_db_STORE_attrib	mariadb_db_STORE_attrib
#define dbd_db_FETCH_attrib	mariadb_db_FETCH_attrib
#define dbd_db_last_insert_id   mariadb_db_last_insert_id
#define dbd_db_data_sources	mariadb_db_data_sources
#define dbd_st_prepare_sv	mariadb_st_prepare_sv
#define dbd_st_execute_iv	mariadb_st_execute_iv
#define dbd_st_fetch		mariadb_st_fetch
#define dbd_st_finish		mariadb_st_finish
#define dbd_st_destroy		mariadb_st_destroy
#define dbd_st_blob_read	mariadb_st_blob_read
#define dbd_st_STORE_attrib	mariadb_st_STORE_attrib
#define dbd_st_FETCH_attrib	mariadb_st_FETCH_attrib
#define dbd_st_last_insert_id	mariadb_st_last_insert_id
#define dbd_bind_ph		mariadb_st_bind_ph

#include <dbd_xsh.h>

/* Compatibility for DBI version prior to 1.634 which do not support dbd_st_execute_iv API */
#ifndef HAVE_DBI_1_634
#define dbd_st_execute		mariadb_st_execute
IV dbd_st_execute_iv(SV *sth, imp_sth_t *imp_sth);
PERL_STATIC_INLINE int dbd_st_execute(SV *sth, imp_sth_t *imp_sth) {
  IV ret = dbd_st_execute_iv(sth, imp_sth);
  if (ret >= INT_MIN && ret <= INT_MAX)
    return ret;
  else         /* overflow */
    return -1; /* -1 is unknown number of rows */
}
#endif

#ifndef HAVE_DBI_1_642
IV mariadb_db_do6(SV *dbh, imp_dbh_t *imp_dbh, SV *statement, SV *attribs, I32 items, I32 ax);
SV *mariadb_st_last_insert_id(SV *sth, imp_sth_t *imp_sth, SV *catalog, SV *schema, SV *table, SV *field, SV *attr);
#endif

#define MARIADB_DR_ATTRIB_GET_SVPS(attribs, key) DBD_ATTRIB_GET_SVP((attribs), "" key "", sizeof((key))-1)

SV* mariadb_dr_my_ulonglong2sv(pTHX_ my_ulonglong val);
#define my_ulonglong2sv(val) mariadb_dr_my_ulonglong2sv(aTHX_ val)

void    mariadb_dr_do_error (SV* h, unsigned int rc, const char *what, const char *sqlstate);

bool mariadb_st_more_results(SV*, imp_sth_t*);

AV* mariadb_db_type_info_all(void);
SV* mariadb_db_quote(SV*, SV*, SV*);

bool mariadb_db_reconnect(SV *h, MYSQL_STMT *stmt);

my_ulonglong mariadb_db_async_result(SV* h, MYSQL_RES** resp);
int mariadb_db_async_ready(SV* h);
