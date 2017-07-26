#include "wget.h"
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>

/*
#define wgint int
enum stype
{
  ST_UNIX,
  ST_VMS,
  ST_WINNT,
  ST_MACOS,
  ST_OS400,
  ST_OTHER
};
enum prot_level
{
  PROT_CLEAR = 'C',
  PROT_SAFE = 'S',
  PROT_CONFIDENTIAL = 'E',
  PROT_PRIVATE = 'P'
};
enum ustype
{
  UST_TYPE_L8,
  UST_MULTINET,
  UST_OTHER
};

typedef enum
{
  NOCONERROR, HOSTERR, CONSOCKERR, CONERROR, CONSSLERR,
  CONIMPOSSIBLE, NEWLOCATION,
  FTPOK, FTPLOGINC, FTPLOGREFUSED, FTPPORTERR, FTPSYSERR,
  FTPNSFOD, FTPUNKNOWNTYPE, FTPRERR,
  FTPSRVERR, FTPRETRINT, FTPRESTFAIL, URLERROR, FOPENERR,
  FOPEN_EXCL_ERR, FWRITEERR, HEOF, GATEWAYTIMEOUT,
  HERR, RETROK, RECLEVELEXC, WRONGCODE,
  FTPINVPASV, FTPNOPASV, FTPNOPBSZ, FTPNOPROT, FTPNOAUTH,
  CONTNOTSUPPORTED, RETRUNNEEDED, RETRFINISHED,
  READERR, TRYLIMEXC, FILEBADFILE, RANGEERR,
  RETRBADPATTERN, PROXERR,
  AUTHFAILED, QUOTEXC, WRITEFAILED, SSLINITFAILED, VERIFCERTERR,
  UNLINKERR, NEWLOCATION_KEEP_POST, CLOSEFAILED, ATTRMISSING, UNKNOWNATTR,
  WARC_ERR, WARC_TMP_FOPENERR, WARC_TMP_FWRITEERR,
  TIMECONV_ERR,
  METALINK_PARSE_ERROR, METALINK_RETR_ERROR,
  METALINK_CHKSUM_ERROR, METALINK_SIG_ERROR, METALINK_MISSING_RESOURCE,
  RETR_WITH_METALINK
} uerr_t;
// 自己手动加的变量定义
typedef struct{
    int x ;
} struct_stat ;

typedef struct {
    char  warc_filename;
    char output_document;

    char noclobber;
} opt_struct;
opt_struct  opt;
char DO_LIST;
char DO_RETR;
char LOG_VERBOSE;
char LEAVE_PENDING;
char ON_YOUR_OWN;
typedef char ip_address;*/ 
static uerr_t ftp_loop_internal (struct url *u, struct fileinfo *f, ccon *con, char **local_file,
                   bool force_full_retrieve)
{
  int count, orig_lp;
  wgint restval, len = 0, qtyread = 0;
  char *tms, *locf;
  const char *tmrate = NULL;
  uerr_t err;
  struct_stat st;

  /* Declare WARC variables. */
  bool warc_enabled = (opt.warc_filename != NULL);
  FILE *warc_tmp = NULL;
  ip_address *warc_ip = NULL;
  wgint last_expected_bytes = 0;

  /* Get the target, and set the name for the message accordingly. */
  if ((f == NULL) && (con->target))
    {
      /* Explicit file (like ".listing"). */
      locf = con->target;
    }
  else
    {
      /* URL-derived file.  Consider "-O file" name. */
      xfree (con->target);
      con->target = url_file_name (u, NULL);
      if (!opt.output_document)
        locf = con->target;
      else
        locf = opt.output_document;
    }

  /* If the output_document was given, then this check was already done and
     the file didn't exist. Hence the !opt.output_document */

  /* If we receive .listing file it is necessary to determine system type of the ftp
     server even if opn.noclobber is given. Thus we must ignore opt.noclobber in
     order to establish connection with the server and get system type. */
  if (opt.noclobber && !opt.output_document && file_exists_p (con->target)
      && !((con->cmd & DO_LIST) && !(con->cmd & DO_RETR)))
    {
      logprintf (LOG_VERBOSE,
                 _("File %s already there; not retrieving.\n"), quote (con->target));
      /* If the file is there, we suppose it's retrieved OK.  */
      return RETROK;
    }

  /* Remove it if it's a link.  */
  remove_link (con->target);

  count = 0;

  if (con->st & ON_YOUR_OWN)
    con->st = ON_YOUR_OWN;

  orig_lp = con->cmd & LEAVE_PENDING ? 1 : 0;

  /* THE loop.  */
  do
    {
      /* Increment the pass counter.  */
      ++count;
      sleep_between_retrievals (count);
      if (con->st & ON_YOUR_OWN)
        {
          con->cmd = 0;
          con->cmd |= (DO_RETR | LEAVE_PENDING);
          if (con->csock != -1)
            con->cmd &= ~ (DO_LOGIN | DO_CWD);
          else
            con->cmd |= (DO_LOGIN | DO_CWD);
        }
      else /* not on your own */
        {
          if (con->csock != -1)
            con->cmd &= ~DO_LOGIN;
          else
            con->cmd |= DO_LOGIN;
          if (con->st & DONE_CWD)
            con->cmd &= ~DO_CWD;
          else
            con->cmd |= DO_CWD;
        }

      /* For file RETR requests, we can write a WARC record.
         We record the file contents to a temporary file. */
      if (warc_enabled && (con->cmd & DO_RETR) && warc_tmp == NULL)
        {
          warc_tmp = warc_tempfile ();
          if (warc_tmp == NULL)
            return WARC_TMP_FOPENERR;

          if (!con->proxy && con->csock != -1)
            {
              warc_ip = (ip_address *) alloca (sizeof (ip_address));
              socket_ip_address (con->csock, warc_ip, ENDPOINT_PEER);
            }
        }

      /* Decide whether or not to restart.  */
      if (con->cmd & DO_LIST)
        restval = 0;
      else if (force_full_retrieve)
        restval = 0;
      else if (opt.start_pos >= 0)
        restval = opt.start_pos;
      else if (opt.always_rest
          && stat (locf, &st) == 0
          && S_ISREG (st.st_mode))
        /* When -c is used, continue from on-disk size.  (Can't use
           hstat.len even if count>1 because we don't want a failed
           first attempt to clobber existing data.)  */
        restval = st.st_size;
      else if (count > 1)
        restval = qtyread;          /* start where the previous run left off */
      else
        restval = 0;

      /* Get the current time string.  */
      tms = datetime_str (time (NULL));
      /* Print fetch message, if opt.verbose.  */
      if (opt.verbose)
        {
          char *hurl = url_string (u, URL_AUTH_HIDE_PASSWD);
          char tmp[256];
          strcpy (tmp, "        ");
          if (count > 1)
            sprintf (tmp, _("(try:%2d)"), count);
          logprintf (LOG_VERBOSE, "--%s--  %s\n  %s => %s\n",
                     tms, hurl, tmp, quote (locf));
#ifdef WINDOWS
          ws_changetitle (hurl);
#endif
          xfree (hurl);
        }
      /* Send getftp the proper length, if fileinfo was provided.  */
      if (f && f->type != FT_SYMLINK)
        len = f->size;
      else
        len = 0;

      /* If we are working on a WARC record, getftp should also write
         to the warc_tmp file. */
      err = getftp (u, len, &qtyread, restval, con, count, &last_expected_bytes,
                    warc_tmp);

      if (con->csock == -1)
        con->st &= ~DONE_CWD;
      else
        con->st |= DONE_CWD;

      switch (err)
        {
        case HOSTERR: case CONIMPOSSIBLE: case FWRITEERR: case FOPENERR:
        case FTPNSFOD: case FTPLOGINC: case FTPNOPASV: case FTPNOAUTH: case FTPNOPBSZ: case FTPNOPROT:
        case UNLINKERR: case WARC_TMP_FWRITEERR: case CONSSLERR: case CONTNOTSUPPORTED:
#ifdef HAVE_SSL
          if (err == FTPNOAUTH)
            logputs (LOG_NOTQUIET, "Server does not support AUTH TLS.\n");
          if (opt.ftps_implicit)
            logputs (LOG_NOTQUIET, "Server does not like implicit FTPS connections.\n");
#endif
          /* Fatal errors, give up.  */
          if (warc_tmp != NULL)
              fclose (warc_tmp);
          return err;
        case CONSOCKERR: case CONERROR: case FTPSRVERR: case FTPRERR:
        case WRITEFAILED: case FTPUNKNOWNTYPE: case FTPSYSERR:
        case FTPPORTERR: case FTPLOGREFUSED: case FTPINVPASV:
        case FOPEN_EXCL_ERR:
          printwhat (count, opt.ntry);
          /* non-fatal errors */
          if (err == FOPEN_EXCL_ERR)
            {
              /* Re-determine the file name. */
              xfree (con->target);
              con->target = url_file_name (u, NULL);
              locf = con->target;
            }
          continue;
        case FTPRETRINT:
          /* If the control connection was closed, the retrieval
             will be considered OK if f->size == len.  */
          if (!f || qtyread != f->size)
            {
              printwhat (count, opt.ntry);
              continue;
            }
          break;
        case RETRFINISHED:
          /* Great!  */
          break;
        default:
          /* Not as great.  */
          abort ();
        }
      tms = datetime_str (time (NULL));
      if (!opt.spider)
        tmrate = retr_rate (qtyread - restval, con->dltime);

      /* If we get out of the switch above without continue'ing, we've
         successfully downloaded a file.  Remember this fact. */
      downloaded_file (FILE_DOWNLOADED_NORMALLY, locf);

      if (con->st & ON_YOUR_OWN)
        {
          fd_close (con->csock);
          con->csock = -1;
        }
      if (!opt.spider)
        {
          bool write_to_stdout = (opt.output_document && HYPHENP (opt.output_document));

          logprintf (LOG_VERBOSE,
                     write_to_stdout
                     ? _("%s (%s) - written to stdout %s[%s]\n\n")
                     : _("%s (%s) - %s saved [%s]\n\n"),
                     tms, tmrate,
                     write_to_stdout ? "" : quote (locf),
                     number_to_static_string (qtyread));
        }
      if (!opt.verbose && !opt.quiet)
        {
          /* Need to hide the password from the URL.  The `if' is here
             so that we don't do the needless allocation every
             time. */
          char *hurl = url_string (u, URL_AUTH_HIDE_PASSWD);
          logprintf (LOG_NONVERBOSE, "%s URL: %s [%s] -> \"%s\" [%d]\n",
                     tms, hurl, number_to_static_string (qtyread), locf, count);
          xfree (hurl);
        }

      if (warc_enabled && (con->cmd & DO_RETR))
        {
          /* Create and store a WARC resource record for the retrieved file. */
          bool warc_res;

          warc_res = warc_write_resource_record (NULL, u->url, NULL, NULL,
                                                  warc_ip, NULL, warc_tmp, -1);

          if (! warc_res)
            return WARC_ERR;

          /* warc_write_resource_record has also closed warc_tmp. */
          warc_tmp = NULL;
        }

      if (con->cmd & DO_LIST)
        /* This is a directory listing file. */
        {
          if (!opt.remove_listing)
            /* --dont-remove-listing was specified, so do count this towards the
               number of bytes and files downloaded. */
            {
              total_downloaded_bytes += qtyread;
              numurls++;
            }

          /* Deletion of listing files is not controlled by --delete-after, but
             by the more specific option --dont-remove-listing, and the code
             to do this deletion is in another function. */
        }
      else if (!opt.spider)
        /* This is not a directory listing file. */
        {
          /* Unlike directory listing files, don't pretend normal files weren't
             downloaded if they're going to be deleted.  People seeding proxies,
             for instance, may want to know how many bytes and files they've
             downloaded through it. */
          total_downloaded_bytes += qtyread;
          numurls++;

          if (opt.delete_after && !input_file_url (opt.input_filename))
            {
              DEBUGP (("\
Removing file due to --delete-after in ftp_loop_internal():\n"));
              logprintf (LOG_VERBOSE, _("Removing %s.\n"), locf);
              if (unlink (locf))
                logprintf (LOG_NOTQUIET, "unlink: %s\n", strerror (errno));
            }
        }

      /* Restore the original leave-pendingness.  */
      if (orig_lp)
        con->cmd |= LEAVE_PENDING;
      else
        con->cmd &= ~LEAVE_PENDING;

      if (local_file)
        *local_file = xstrdup (locf);

      if (warc_tmp != NULL)
        fclose (warc_tmp);

      return RETROK;
    } while (!opt.ntry || (count < opt.ntry));

  if (con->csock != -1 && (con->st & ON_YOUR_OWN))
    {
      fd_close (con->csock);
      con->csock = -1;
    }

  if (warc_tmp != NULL)
    fclose (warc_tmp);

  return TRYLIMEXC;
}



/* The wrapper that calls an appropriate routine according to contents
   of URL.  Inherently, its capabilities are limited on what can be
   encoded into a URL.  */
uerr_t
ftp_loop (struct url *u, char **local_file, int *dt, struct url *proxy,
          bool recursive, bool glob)
{
  ccon con;                     /* FTP connection */
  uerr_t res;

  *dt = 0;

  xzero (con);

  con.csock = -1;
  con.st = ON_YOUR_OWN;
  con.rs = ST_UNIX;
  con.id = NULL;
  con.proxy = proxy;

  /* If the file name is empty, the user probably wants a directory
     index.  We'll provide one, properly HTML-ized.  Unless
     opt.htmlify is 0, of course.  :-) */
  if (!*u->file && !recursive)
    {
      struct fileinfo *f;
      res = ftp_get_listing (u, &con, &f);

      if (res == RETROK)
        {
          if (opt.htmlify && !opt.spider)
            {
              char *filename = (opt.output_document
                                ? xstrdup (opt.output_document)
                                : (con.target ? xstrdup (con.target)
                                   : url_file_name (u, NULL)));
              res = ftp_index (filename, u, f);
              if (res == FTPOK && opt.verbose)
                {
                  if (!opt.output_document)
                    {
                      struct_stat st;
                      wgint sz;
                      if (stat (filename, &st) == 0)
                        sz = st.st_size;
                      else
                        sz = -1;
                      logprintf (LOG_NOTQUIET,
                                 _("Wrote HTML-ized index to %s [%s].\n"),
                                 quote (filename), number_to_static_string (sz));
                    }
                  else
                    logprintf (LOG_NOTQUIET,
                               _("Wrote HTML-ized index to %s.\n"),
                               quote (filename));
                }
              xfree (filename);
            }
          freefileinfo (f);
        }
    }
  else
    {
      bool ispattern = false;
      if (glob)
        {
          /* Treat the URL as a pattern if the file name part of the
             URL path contains wildcards.  (Don't check for u->file
             because it is unescaped and therefore doesn't leave users
             the option to escape literal '*' as %2A.)  */
          char *file_part = strrchr (u->path, '/');
          if (!file_part)
            file_part = u->path;
          ispattern = has_wildcards_p (file_part);
        }
      if (ispattern || recursive || opt.timestamping || opt.preserve_perm)
        {
          /* ftp_retrieve_glob is a catch-all function that gets called
             if we need globbing, time-stamping, recursion or preserve
             permissions.  Its third argument is just what we really need.  */
          res = ftp_retrieve_glob (u, &con,
                                   ispattern ? GLOB_GLOBALL : GLOB_GETONE);
        }
      else
        res = ftp_loop_internal (u, NULL, &con, local_file, false);
    }
  if (res == FTPOK)
    res = RETROK;
  if (res == RETROK)
    *dt |= RETROKF;
  /* If a connection was left, quench it.  */
  if (con.csock != -1)
    fd_close (con.csock);
  xfree (con.id);
  xfree (con.target);
  return res;
}
int main(){

    return 0;
}
