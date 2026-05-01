/*
 * Image plugin to VDR (C++)
 *
 * (c) 2004-2011 Andreas Brachold <vdr07 at deltab.de>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#include <malloc.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>

#include "commands.h"
#include "setup-image.h"

// Helper to safely quote a string for use in a shell command.
// This replaces every single quote with '\'', effectively breaking out of the
// surrounding quotes to insert a literal quote, then re-entering.
// The result is a single, unbreakable token for the shell.
// The caller must free the returned string.
static char *ShellQuote(const char *s)
{
  if (!s)
    return strdup("''");

  int l = strlen(s);
  // Worst case: every char is a single quote, needing 4 chars ('\'')
  // +2 for the surrounding quotes and +1 for the null terminator.
  char *r = (char *)malloc(4 * l + 3);
  if (!r)
    return NULL;

  char *p = r;
  *p++ = '\'';
  for (int i = 0; i < l; i++) {
    if (s[i] == '\'') {
      memcpy(p, "'\\''", 4);
      p += 4;
    } else {
      *p++ = s[i];
    }
  }
  *p++ = '\'';
  *p = '\0';
  return r;
}

// --- cImageCommand -------------------------------------------------------------

char *cImageCommand::m_szLastResult = NULL;

cImageCommand::cImageCommand(void)
: m_szTitle(NULL)
, m_szCommand(NULL)
, m_bConfirm(false)
{

}

cImageCommand::~cImageCommand()
{
    if(m_szTitle)
    {
        free(m_szTitle);
        m_szTitle = NULL;
    }
    if(m_szCommand)
    {
        free(m_szCommand);
        m_szCommand = NULL;
    }
}

bool cImageCommand::Parse(const char *s)
{
  const char *p = strchr(s, ':');
  if(p)
  {
    int l = p - s;
    if(l > 0)
    {
      m_szTitle = MALLOC(char, l + 1);
      stripspace(strn0cpy(m_szTitle, s, l + 1));
      if(!isempty(m_szTitle))
      {
        int len = strlen(m_szTitle);
        if(len > 1 && m_szTitle[len - 1] == '?')
        {
          m_bConfirm = true;
          m_szTitle[len - 1] = 0;
        }
        m_szCommand = stripspace(strdup(skipspace(p + 1)));
        if (!isempty(m_szCommand)) {
          return true;
        }
        free(m_szCommand);
        m_szCommand = NULL;
      }
      free(m_szTitle);
      m_szTitle = NULL;
    }
  }
  return false;
}



const char *cImageCommand::Execute(const char *szFileName)
{
  char *szCmdBuf = NULL;
  
  if(m_szLastResult)
  {
    free(m_szLastResult);
    m_szLastResult = NULL;
  }

  // Combine command and filename
  if(szFileName && m_szCommand) {
    char *quotedFileNameRaw = ShellQuote(szFileName);
    if (quotedFileNameRaw) {
        std::string command = m_szCommand;
        std::string replacement = quotedFileNameRaw;
        free(quotedFileNameRaw);

        size_t start_pos = 0;
        bool found = false;
        while((start_pos = command.find("%s", start_pos)) != std::string::npos) {
            command.replace(start_pos, 2, replacement);
            start_pos += replacement.length();
            found = true;
        }

        if (!found) {
            command += " ";
            command += replacement;
        }
        szCmdBuf = strdup(command.c_str());
    }
  }

  const char *szCmd = szCmdBuf ? szCmdBuf : m_szCommand;
  dsyslog("imageplugin: executing command '%s'", szCmd);

  // Set environment
  ImageSetup.SetEnv();

  FILE *p = popen(szCmd, "r");
  if(p)
  {
    std::string result;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), p) != NULL) {
        result.append(buffer);
    }
    pclose(p);
    if (!result.empty()) {
        m_szLastResult = strdup(result.c_str());
    }
  } 
  else
    esyslog("imageplugin: can't open pipe for command '%s'", szCmd);
  
  if(szCmdBuf)
    free(szCmdBuf);
  
  return m_szLastResult;
}

cImageCommands::cImageCommands(void)
: m_szFileName(NULL)
{

}

cImageCommands::~ cImageCommands()
{
	Clear();
}

void cImageCommands::Clear(void) 
{
  if(m_szFileName)
    free(m_szFileName);
  m_szFileName = NULL;
  cList < cImageCommand >::Clear();
}


bool cImageCommands::Load(const char *szFileName/* = NULL*/, bool bAllowComments /*=true*/, bool bMustExist /*= false*/)
{
  Clear();
  if(szFileName)
  {
    m_szFileName = strdup(szFileName);
    m_bAllowComments = bAllowComments;
  }
    
  bool bRet = !bMustExist;
  if(m_szFileName && access(m_szFileName, F_OK) == 0)
  {
    isyslog("imageplugin: loading %s", m_szFileName);
    FILE *f = fopen(m_szFileName, "r");
    if(f)
    {
      int n = 0;
      char szBuf[8192];
      bRet = true;
      while(fgets(szBuf, sizeof(szBuf), f) != NULL)
      {
        ++n;
        if(m_bAllowComments)
        {
          char *p = strchr(szBuf, '#');
          if(p)
            *p = 0;
        }
        stripspace(szBuf);
        if(!isempty(szBuf))
				{
          cImageCommand *l = new cImageCommand;
          if(l->Parse(szBuf))
          {
            Add(l);
          }
          else
          {
            esyslog("imageplugin: error in %s, line %d\n",m_szFileName, n);
            delete l;
            bRet = false;
            break;
          }
				}
			}
      fclose(f);
    } else
    {
      esyslog("imageplugin: error %s: %m", m_szFileName);
      bRet = false;
    }
	}
  if(!bRet)
    esyslog("imageplugin: error while reading '%s'\n", m_szFileName);
  return bRet;
}
