/*
 * The Shibboleth License, Version 1.
 * Copyright (c) 2002
 * University Corporation for Advanced Internet Development, Inc.
 * All rights reserved
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution, if any, must include
 * the following acknowledgment: "This product includes software developed by
 * the University Corporation for Advanced Internet Development
 * <http://www.ucaid.edu>Internet2 Project. Alternately, this acknowledegement
 * may appear in the software itself, if and wherever such third-party
 * acknowledgments normally appear.
 *
 * Neither the name of Shibboleth nor the names of its contributors, nor
 * Internet2, nor the University Corporation for Advanced Internet Development,
 * Inc., nor UCAID may be used to endorse or promote products derived from this
 * software without specific prior written permission. For written permission,
 * please contact shibboleth@shibboleth.org
 *
 * Products derived from this software may not be called Shibboleth, Internet2,
 * UCAID, or the University Corporation for Advanced Internet Development, nor
 * may Shibboleth appear in their name, without prior written permission of the
 * University Corporation for Advanced Internet Development.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND WITH ALL FAULTS. ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE, AND NON-INFRINGEMENT ARE DISCLAIMED AND THE ENTIRE RISK
 * OF SATISFACTORY QUALITY, PERFORMANCE, ACCURACY, AND EFFORT IS WITH LICENSEE.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER, CONTRIBUTORS OR THE UNIVERSITY
 * CORPORATION FOR ADVANCED INTERNET DEVELOPMENT, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* isapi_shib.cpp - Shibboleth ISAPI filter

   Scott Cantor
   8/23/02
*/

// SAML Runtime
#include <saml/saml.h>
#include <shib/shib.h>
#include <shib/shib-threads.h>
#include <shib-target/shib-target.h>

#include <log4cpp/Category.hh>

#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <httpfilt.h>
#include <httpext.h>

#include <cgiparse.h>

using namespace std;
using namespace log4cpp;
using namespace saml;
using namespace shibboleth;
using namespace shibtarget;

// globals
namespace {
    HINSTANCE g_hinstDLL;
    ShibTargetConfig* g_Config = NULL;
    vector<string> g_Sites;
}

BOOL LogEvent(
    LPCSTR  lpUNCServerName,
    WORD  wType,
    DWORD  dwEventID,
    PSID  lpUserSid,
    LPCSTR  message)
{
    LPCSTR  messages[] = {message, NULL};
    
    HANDLE hElog = RegisterEventSource(lpUNCServerName, "Shibboleth ISAPI Filter");
    BOOL res = ReportEvent(hElog, wType, 0, dwEventID, lpUserSid, 1, 0, messages, NULL);
    return (DeregisterEventSource(hElog) && res);
}

extern "C" __declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    if (fdwReason==DLL_PROCESS_ATTACH)
        g_hinstDLL=hinstDLL;
    return TRUE;
}

extern "C" BOOL WINAPI GetExtensionVersion(HSE_VERSION_INFO* pVer)
{
    if (!pVer)
        return FALSE;
        
    if (!g_Config)
    {
        LogEvent(NULL, EVENTLOG_ERROR_TYPE, 2100, NULL,
                "Extension mode startup not possible, is the DLL loaded as a filter?");
        return FALSE;
    }

    pVer->dwExtensionVersion=HSE_VERSION;
    strncpy(pVer->lpszExtensionDesc,"Shibboleth ISAPI Extension",HSE_MAX_EXT_DLL_NAME_LEN-1);
    return TRUE;
}

extern "C" BOOL WINAPI GetFilterVersion(PHTTP_FILTER_VERSION pVer)
{
    if (!pVer)
        return FALSE;

    try
    {
        ShibTargetConfig::preinit();
        g_Config = &(ShibTargetConfig::init(SHIBTARGET_SHIRE, getenv("SHIBCONFIG")));
        ShibINI& ini = g_Config->getINI();

        Category& log=Category::getInstance("isapi_shib.GetFilterVersion");

        // Read site-specific settings for each instance ID we can find.
        unsigned short i=1;
        char iid[8];
        sprintf(iid,"%u",i++);
        string hostname;
        while (ini.get_tag("isapi",iid,false,&hostname))
        {
            log.info("configuring for site ID (%d), hostname (%s)",i-1,hostname.empty() ? "null" : hostname.c_str());

            // If no section exists for the host, mark it as a "skip" site.
            if (hostname == "skip")
            {
                log.info("skipping site ID (%d)",i-1);
                hostname.erase();
            }

            g_Sites.push_back(hostname);
            sprintf(iid,"%u",i++);
            hostname.erase();
        }
    }
    catch (SAMLException&)
    {
        LogEvent(NULL, EVENTLOG_ERROR_TYPE, 2100, NULL,
                "Filter startup failed with SAML exception, check shire log for help.");
        return FALSE;
    }
    catch (runtime_error& e)
    {
        LogEvent(NULL, EVENTLOG_ERROR_TYPE, 2100, NULL, e.what());
        return FALSE;
    }
    catch (...)
    {
        LogEvent(NULL, EVENTLOG_ERROR_TYPE, 2100, NULL, "Filter startup failed with unexpected exception.");
        return FALSE;
    }

    pVer->dwFilterVersion=HTTP_FILTER_REVISION;
    strncpy(pVer->lpszFilterDesc,"Shibboleth ISAPI Filter",SF_MAX_FILTER_DESC_LEN);
    pVer->dwFlags=(SF_NOTIFY_ORDER_HIGH |
                   SF_NOTIFY_SECURE_PORT |
                   SF_NOTIFY_NONSECURE_PORT |
                   SF_NOTIFY_PREPROC_HEADERS |
                   SF_NOTIFY_LOG);
    LogEvent(NULL, EVENTLOG_INFORMATION_TYPE, 7701, NULL, "Filter initialized...");
    return TRUE;
}

extern "C" BOOL WINAPI TerminateExtension(DWORD)
{
    return TRUE;    // cleanup should happen when filter unloads
}

extern "C" BOOL WINAPI TerminateFilter(DWORD)
{
    if (g_Config)
        g_Config->shutdown();
    g_Config = NULL;
    LogEvent(NULL, EVENTLOG_INFORMATION_TYPE, 7701, NULL, "Filter shut down...");
    return TRUE;
}

/* Next up, some suck-free versions of various APIs.

   You DON'T require people to guess the buffer size and THEN tell them the right size.
   Returning an LPCSTR is apparently way beyond their ken. Not to mention the fact that
   constant strings aren't typed as such, making it just that much harder. These versions
   are now updated to use a special growable buffer object, modeled after the standard
   string class. The standard string won't work because they left out the option to
   pre-allocate a non-constant buffer.
*/

class dynabuf
{
public:
    dynabuf() { bufptr=NULL; buflen=0; }
    dynabuf(size_t s) { bufptr=new char[buflen=s]; *bufptr=0; }
    ~dynabuf() { delete[] bufptr; }
    size_t length() const { return bufptr ? strlen(bufptr) : 0; }
    size_t size() const { return buflen; }
    bool empty() const { return length()==0; }
    void reserve(size_t s, bool keep=false);
    void erase() { if (bufptr) memset(bufptr,0,buflen); }
    operator char*() { return bufptr; }
    bool operator ==(const char* s) const;
    bool operator !=(const char* s) const { return !(*this==s); }
private:
    char* bufptr;
    size_t buflen;
};

void dynabuf::reserve(size_t s, bool keep)
{
    if (s<=buflen)
        return;
    char* p=new char[s];
    if (keep)
        while (buflen--)
            p[buflen]=bufptr[buflen];
    buflen=s;
    delete[] bufptr;
    bufptr=p;
}

bool dynabuf::operator==(const char* s) const
{
    if (buflen==NULL || s==NULL)
        return (buflen==NULL && s==NULL);
    else
        return strcmp(bufptr,s)==0;
}

void GetServerVariable(PHTTP_FILTER_CONTEXT pfc, LPSTR lpszVariable, dynabuf& s, DWORD size=80, bool bRequired=true)
    throw (bad_alloc, DWORD)
{
    s.erase();
    s.reserve(size);
    size=s.size();

    while (!pfc->GetServerVariable(pfc,lpszVariable,s,&size))
    {
        // Grumble. Check the error.
        DWORD e=GetLastError();
        if (e==ERROR_INSUFFICIENT_BUFFER)
            s.reserve(size);
        else
            break;
    }
    if (bRequired && s.empty())
        throw ERROR_NO_DATA;
}

void GetServerVariable(LPEXTENSION_CONTROL_BLOCK lpECB, LPSTR lpszVariable, dynabuf& s, DWORD size=80, bool bRequired=true)
    throw (bad_alloc, DWORD)
{
    s.erase();
    s.reserve(size);
    size=s.size();

    while (lpECB->GetServerVariable(lpECB->ConnID,lpszVariable,s,&size))
    {
        // Grumble. Check the error.
        DWORD e=GetLastError();
        if (e==ERROR_INSUFFICIENT_BUFFER)
            s.reserve(size);
        else
            break;
    }
    if (bRequired && s.empty())
        throw ERROR_NO_DATA;
}

void GetHeader(PHTTP_FILTER_PREPROC_HEADERS pn, PHTTP_FILTER_CONTEXT pfc,
               LPSTR lpszName, dynabuf& s, DWORD size=80, bool bRequired=true)
    throw (bad_alloc, DWORD)
{
    s.erase();
    s.reserve(size);
    size=s.size();

    while (!pn->GetHeader(pfc,lpszName,s,&size))
    {
        // Grumble. Check the error.
        DWORD e=GetLastError();
        if (e==ERROR_INSUFFICIENT_BUFFER)
            s.reserve(size);
        else
            break;
    }
    if (bRequired && s.empty())
        throw ERROR_NO_DATA;
}

inline char hexchar(unsigned short s)
{
    return (s<=9) ? ('0' + s) : ('A' + s - 10);
}

string url_encode(const char* url) throw (bad_alloc)
{
    static char badchars[]="\"\\+<>#%{}|^~[]`;/?:@=&";
    string s;
    for (const char* pch=url; *pch; pch++)
    {
        if (strchr(badchars,*pch)!=NULL || *pch<=0x1F || *pch>=0x7F)
            s=s + '%' + hexchar(*pch >> 4) + hexchar(*pch & 0x0F);
        else
            s+=*pch;
    }
    return s;
}

void get_target_and_appid(
    PHTTP_FILTER_CONTEXT pfc, PHTTP_FILTER_PREPROC_HEADERS pn, const char* hostname, string& target, string& appid
    )
{
    dynabuf port(10);
    dynabuf url(256);
    GetServerVariable(pfc,"SERVER_PORT",port,10);
    GetHeader(pn,pfc,"url",url,256,false);
    
    // First get the appid using the normalized hostname.
    ApplicationMapper mapper;
    appid = mapper->getApplicationFromParsedURL((pfc->fIsSecurePort ? "https" : "http"), hostname, atoi(port), url);

    target=static_cast<char*>(url);
    if (port!=(pfc->fIsSecurePort ? "443" : "80"))
        target = ':' + static_cast<char*>(port) + target;

    // For the target, we use the "normalizeRequest" tag to decide how to set the server's name.
    string tag;
    if (g_Config->getINI().get_tag(appid,"normalizeRequest",true,&tag) && ShibINI::boolean(tag))
    {
        target=string(pfc->fIsSecurePort ? "https://" : "http://") + hostname + target;
    }
    else
    {
        GetServerVariable(pfc,"SERVER_NAME",url);
        target=string(pfc->fIsSecurePort ? "https://" : "http://") + static_cast<char*>(url) + target;
    }
}

string get_shire_location(const char* application_id, const char* target)
{
    string shireURL;
    if (g_Config->getINI().get_tag(application_id,"shireURL",true,&shireURL) && !shireURL.empty())
    {
        if (shireURL[0]!='/')
            return shireURL;
        const char* colon=strchr(target,':');
        const char* slash=strchr(colon+3,'/');
        string s(target,slash-target);
        s+=shireURL;
        return s;
    }
    return shireURL;
}

DWORD WriteClientError(PHTTP_FILTER_CONTEXT pfc, const char* msg)
{
    LogEvent(NULL, EVENTLOG_ERROR_TYPE, 2100, NULL, msg);
    static const char* ctype="Content-Type: text/html\r\n";
    pfc->AddResponseHeaders(pfc,const_cast<char*>(ctype),0);
    pfc->ServerSupportFunction(pfc,SF_REQ_SEND_RESPONSE_HEADER,"200 OK",0,0);
    static const char* xmsg="<HTML><HEAD><TITLE>Shibboleth Filter Error</TITLE></HEAD><BODY>"
                            "<H1>Shibboleth Filter Error</H1>";
    DWORD resplen=strlen(xmsg);
    pfc->WriteClient(pfc,(LPVOID)xmsg,&resplen,0);
    resplen=strlen(msg);
    pfc->WriteClient(pfc,(LPVOID)msg,&resplen,0);
    static const char* xmsg2="</BODY></HTML>";
    resplen=strlen(xmsg2);
    pfc->WriteClient(pfc,(LPVOID)xmsg2,&resplen,0);
    return SF_STATUS_REQ_FINISHED;
}

DWORD WriteClientError(PHTTP_FILTER_CONTEXT pfc, const char* filename, ShibMLP& mlp)
{
    ifstream infile(filename);
    if (!infile)
        return WriteClientError(pfc,"Unable to open error template, check settings.");   
    string res = mlp.run(infile);

    static const char* ctype="Content-Type: text/html\r\n";
    pfc->AddResponseHeaders(pfc,const_cast<char*>(ctype),0);
    pfc->ServerSupportFunction(pfc,SF_REQ_SEND_RESPONSE_HEADER,"200 OK",0,0);
    DWORD resplen=res.length();
    pfc->WriteClient(pfc,(LPVOID)res.c_str(),&resplen,0);
    return SF_STATUS_REQ_FINISHED;
}

extern "C" DWORD WINAPI HttpFilterProc(PHTTP_FILTER_CONTEXT pfc, DWORD notificationType, LPVOID pvNotification)
{
    // Is this a log notification?
    if (notificationType==SF_NOTIFY_LOG)
    {
        if (pfc->pFilterContext)
            ((PHTTP_FILTER_LOG)pvNotification)->pszClientUserName=static_cast<LPCSTR>(pfc->pFilterContext);
        return SF_STATUS_REQ_NEXT_NOTIFICATION;
    }

    PHTTP_FILTER_PREPROC_HEADERS pn=(PHTTP_FILTER_PREPROC_HEADERS)pvNotification;
    try
    {
        // Determine web site number. This can't really fail, I don't think.
        dynabuf buf(128);
        ULONG site_id=0;
        GetServerVariable(pfc,"INSTANCE_ID",buf,10);
        if ((site_id=strtoul(buf,NULL,10))==0)
            return WriteClientError(pfc,"IIS site instance appears to be invalid.");

        // Match site instance to site settings.
        if (site_id>g_Sites.size() || g_Sites[site_id-1].length()==0)
            return SF_STATUS_REQ_NEXT_NOTIFICATION;
        string& site=g_Sites[site_id-1];
        
        string application_id;
        string target_url;
        get_target_and_appid(pfc,pn,site.c_str(),target_url,application_id);
        string shire_url=get_shire_location(application_id.c_str(),target_url.c_str());

        // If the user is accessing the SHIRE acceptance point, pass it on.
        if (target_url.find(shire_url)!=string::npos)
            return SF_STATUS_REQ_NEXT_NOTIFICATION;

        // Now check the policy for this application.
        string tag;
        ShibINI& ini=g_Config->getINI();
        if (!ini.get_tag(application_id,"requireSession",true,&tag) || !ShibINI::boolean(tag))
            return SF_STATUS_REQ_NEXT_NOTIFICATION;

        // SSL content check.
        if (ini.get_tag(application_id,"contentSSLOnly",true,&tag) && ShibINI::boolean(tag) && !pfc->fIsSecurePort)
        {
            return WriteClientError(pfc,
                "This server is configured to deny non-SSL requests for secure resources. "
                "Try your request again using https instead of http.");
        }

        ostringstream threadid;
        threadid << "[" << getpid() << "] shire" << '\0';
        saml::NDC ndc(threadid.str().c_str());

        // Set SHIRE policies.
        SHIREConfig config;
        config.checkIPAddress = (ini.get_tag(application_id,"checkIPAddress",true,&tag) && ShibINI::boolean(tag));
        config.lifetime=config.timeout=0;
        tag.erase();
        if (ini.get_tag(application_id, "authLifetime", true, &tag))
            config.lifetime=strtoul(tag.c_str(),NULL,10);
        tag.erase();
        if (ini.get_tag(application_id, "authTimeout", true, &tag))
            config.timeout=strtoul(tag.c_str(),NULL,10);

        // Pull the config data we need to handle the various possible conditions.
        string shib_cookie;
        if (!ini.get_tag(application_id, "cookieName", true, &shib_cookie))
            return WriteClientError(pfc,"The cookieName configuration setting is missing, check configuration.");
    
        string wayfLocation;
        if (!ini.get_tag(application_id, "wayfURL", true, &wayfLocation))
            return WriteClientError(pfc,"The wayfURL configuration setting is missing, check configuration.");
    
        string shireError;
        if (!ini.get_tag(application_id, "shireError", true, &shireError))
            return WriteClientError(pfc,"The shireError configuration setting is missing, check configuration.");

        string accessError;
        if (!ini.get_tag(application_id, "accessError", true, &shireError))
            return WriteClientError(pfc,"The accessError configuration setting is missing, check configuration.");
        
        SHIRE shire(config, shire_url.c_str());

        // Check for authentication cookie.
        const char* session_id=NULL;
        GetHeader(pn,pfc,"Cookie:",buf,128,false);
        Category::getInstance("isapi_shib.HttpFilterProc").debug("cookie header is {%s}",(const char*)buf);
        if (buf.empty() || !(session_id=strstr(buf,shib_cookie.c_str())) || *(session_id+shib_cookie.length())!='=')
        {
            // Redirect to WAYF.
            char timebuf[16];
            sprintf(timebuf,"%u",time(NULL));
            string wayf("Location: ");
            wayf+=wayfLocation + "?shire=" + url_encode(shire_url.c_str()) + "&target=" + url_encode(target_url.c_str()) +
                "&time=" + timebuf + "&providerId=" + application_id + "\r\n";
            // Insert the headers.
            pfc->AddResponseHeaders(pfc,const_cast<char*>(wayf.c_str()),0);
            pfc->ServerSupportFunction(pfc,SF_REQ_SEND_RESPONSE_HEADER,"302 Please Wait",0,0);
            return SF_STATUS_REQ_FINISHED;
        }

        session_id+=shib_cookie.length() + 1;	/* Skip over the '=' */
        char* cookieend=strchr(session_id,';');
        if (cookieend)
            *cookieend = '\0';	/* Ignore anyting after a ; */
  
        // Make sure this session is still valid.
        RPCError* status = NULL;
        ShibMLP markupProcessor;
        bool has_tag = ini.get_tag(application_id, "supportContact", true, &tag);
        markupProcessor.insert("supportContact", has_tag ? tag : "");
        has_tag = ini.get_tag(application_id, "logoLocation", true, &tag);
        markupProcessor.insert("logoLocation", has_tag ? tag : "");
        markupProcessor.insert("requestURL", target_url);
    
        dynabuf abuf(16);
        GetServerVariable(pfc,"REMOTE_ADDR",abuf,16);
        try {
            status = shire.sessionIsValid(session_id, abuf, application_id.c_str());
        }
        catch (ShibTargetException &e) {
            markupProcessor.insert("errorType", "SHIRE Processing Error");
            markupProcessor.insert("errorText", e.what());
            markupProcessor.insert("errorDesc", "An error occurred while processing your request.");
            return WriteClientError(pfc, shireError.c_str(), markupProcessor);
        }
        catch (...) {
            markupProcessor.insert("errorType", "SHIRE Processing Error");
            markupProcessor.insert("errorText", "Unexpected Exception");
            markupProcessor.insert("errorDesc", "An error occurred while processing your request.");
            return WriteClientError(pfc, shireError.c_str(), markupProcessor);
        }
    
        // Check the status
        if (status->isError()) {
            if (status->isRetryable()) {
                // Redirect to WAYF.
                delete status;
                char timebuf[16];
                sprintf(timebuf,"%u",time(NULL));
                string wayf("Location: ");
                wayf+=wayfLocation + "?shire=" + url_encode(shire_url.c_str()) + "&target=" + url_encode(target_url.c_str()) +
                    "&time=" + timebuf + "&providerId=" + application_id + "\r\n";
                // Insert the headers.
                pfc->AddResponseHeaders(pfc,const_cast<char*>(wayf.c_str()),0);
                pfc->ServerSupportFunction(pfc,SF_REQ_SEND_RESPONSE_HEADER,"302 Please Wait",0,0);
                return SF_STATUS_REQ_FINISHED;
            }
            else {
                // return the error page to the user
                markupProcessor.insert(*status);
                delete status;
                return WriteClientError(pfc, shireError.c_str(), markupProcessor);
            }
        }
        delete status;
    
        // Move to RM phase.
        RMConfig rm_config;
        rm_config.checkIPAddress = config.checkIPAddress;
        RM rm(rm_config);

        // Get the attributes.
        vector<SAMLAssertion*> assertions;
        SAMLAuthenticationStatement* sso_statement=NULL;
        status = rm.getAssertions(session_id, buf, application_id.c_str(), assertions, &sso_statement);
    
        if (status->isError()) {
            string rmError;
            if (!ini.get_tag(application_id, "rmError", true, &shireError))
                return WriteClientError(pfc,"The rmError configuration setting is missing, check configuration.");
    
            markupProcessor.insert(*status);
            delete status;
            return WriteClientError(pfc, rmError.c_str(), markupProcessor);
        }
        delete status;

        // Only allow a single assertion...
        if (assertions.size() > 1) {
            for (int k = 0; k < assertions.size(); k++)
              delete assertions[k];
            delete sso_statement;
            return WriteClientError(pfc, accessError.c_str(), markupProcessor);
        }

        // Get the AAP providers, which contain the attribute policy info.
        Iterator<IAAP*> provs=g_Config->getAAPProviders();
    
        // Clear out the list of mapped attributes
        while (provs.hasNext())
        {
            IAAP* aap=provs.next();
            aap->lock();
            try
            {
                Iterator<const IAttributeRule*> rules=aap->getAttributeRules();
                while (rules.hasNext())
                {
                    const char* header=rules.next()->getHeader();
                    if (header)
                        pn->SetHeader(pfc,const_cast<char*>(header),"");
                }
            }
            catch(...)
            {
                aap->unlock();
                for (int k = 0; k < assertions.size(); k++)
                  delete assertions[k];
                delete sso_statement;
                throw;
            }
            aap->unlock();
        }
        provs.reset();

        // Clear relevant headers.
        pn->SetHeader(pfc,"remote-user:","");
        pn->SetHeader(pfc,"Shib-Attributes:","");
        pn->SetHeader(pfc,"Shib-Origin-Site:","");
        pn->SetHeader(pfc,"Shib-Authentication-Method:","");

        pn->SetHeader(pfc,"Shib-Application-ID:","");
        pn->SetHeader(pfc,"Shib-Application-ID:",const_cast<char*>(application_id.c_str()));

        // Maybe export the assertion.
        if (ini.get_tag(application_id,"exportAssertion",true,&tag) && ShibINI::boolean(tag))
        {
            string assertion;
            RM::serialize(*(assertions[0]), assertion);
            string::size_type lfeed;
            while ((lfeed=assertion.find('\n'))!=string::npos)
                assertion.erase(lfeed,1);
            pn->SetHeader(pfc,"Shib-Attributes:",const_cast<char*>(assertion.c_str()));
        }
        
        if (sso_statement)
        {
            auto_ptr_char os(sso_statement->getSubject()->getNameQualifier());
            auto_ptr_char am(sso_statement->getAuthMethod());
            pn->SetHeader(pfc,"Shib-Origin-Site:", const_cast<char*>(os.get()));
            pn->SetHeader(pfc,"Shib-Authentication-Method:", const_cast<char*>(am.get()));
        }

        // Export the attributes.
        Iterator<SAMLAssertion*> a_iter(assertions);
        while (a_iter.hasNext()) {
            SAMLAssertion* assert=a_iter.next();
            Iterator<SAMLStatement*> statements=assert->getStatements();
            while (statements.hasNext()) {
                SAMLAttributeStatement* astate=dynamic_cast<SAMLAttributeStatement*>(statements.next());
                if (!astate)
                    continue;
                Iterator<SAMLAttribute*> attrs=astate->getAttributes();
                while (attrs.hasNext()) {
                    SAMLAttribute* attr=attrs.next();
        
                    // Are we supposed to export it?
                    AAP wrapper(g_Config->getAAPProviders(),attr->getName(),attr->getNamespace());
                    if (wrapper.fail())
                        continue;
                
                    Iterator<string> vals=attr->getSingleByteValues();
                    if (!strcmp(wrapper->getHeader(),"REMOTE_USER") && vals.hasNext()) {
                        char* principal=const_cast<char*>(vals.next().c_str());
                        pn->SetHeader(pfc,"remote-user:",principal);
                        pfc->pFilterContext=pfc->AllocMem(pfc,strlen(principal)+1,0);
                        if (pfc->pFilterContext)
                            strcpy(static_cast<char*>(pfc->pFilterContext),principal);
                    }
                    else {
                        string header;
                        for (int it = 0; vals.hasNext(); it++) {
                            string value = vals.next();
                            for (string::size_type pos = value.find_first_of(";", string::size_type(0)); pos != string::npos; pos = value.find_first_of(";", pos)) {
                                value.insert(pos, "\\");
                                pos += 2;
                            }
                            if (it == 0)
                                header=value;
                            else
                                header=header + ';' + value;
                        }
                        string hname2=string(wrapper->getHeader()) + ':';
                        pn->SetHeader(pfc,const_cast<char*>(hname2.c_str()),const_cast<char*>(header.c_str()));
        	        }
                }
            }
        }
    
        // clean up memory
        for (int k = 0; k < assertions.size(); k++)
          delete assertions[k];
        delete sso_statement;

        return SF_STATUS_REQ_NEXT_NOTIFICATION;
    }
    catch(bad_alloc)
    {
        return WriteClientError(pfc,"Out of Memory");
    }
    catch(DWORD e)
    {
        if (e==ERROR_NO_DATA)
            return WriteClientError(pfc,"A required variable or header was empty.");
        else
            return WriteClientError(pfc,"Server detected unexpected IIS error.");
    }
    catch(...)
    {
        return WriteClientError(pfc,"Server caught an unknown exception.");
    }

    return WriteClientError(pfc,"Server reached unreachable code!");
}

void get_target_and_appid(LPEXTENSION_CONTROL_BLOCK lpECB, const char* hostname, string& target, string& appid)
{
    dynabuf ssl(5);
    dynabuf port(10);
    dynabuf url(256);
    GetServerVariable(lpECB,"HTTPS",ssl,5);
    GetServerVariable(lpECB,"SERVER_PORT",port,10);
    GetServerVariable(lpECB,"URL",url,255);
    bool SSL=(ssl=="on");
    
    // First get the appid using the normalized hostname.
    ApplicationMapper mapper;
    appid = mapper->getApplicationFromParsedURL((SSL ? "https" : "http"), hostname, atoi(port), url);

    target=static_cast<char*>(url);
    if (port!=(SSL ? "443" : "80"))
        target = ':' + static_cast<char*>(port) + target;

    // For the target, we use the "normalizeRequest" tag to decide how to set the server's name.
    string tag;
    if (g_Config->getINI().get_tag(appid,"normalizeRequest",true,&tag) && ShibINI::boolean(tag))
    {
        target=string(SSL ? "https://" : "http://") + hostname + target;
    }
    else
    {
        GetServerVariable(lpECB,"SERVER_NAME",url);
        target=string(SSL ? "https://" : "http://") + static_cast<char*>(url) + target;
    }
}

DWORD WriteClientError(LPEXTENSION_CONTROL_BLOCK lpECB, const char* msg)
{
    LogEvent(NULL, EVENTLOG_ERROR_TYPE, 2100, NULL, msg);
    static const char* ctype="Content-Type: text/html\r\n";
    lpECB->ServerSupportFunction(lpECB->ConnID,HSE_REQ_SEND_RESPONSE_HEADER,"200 OK",0,(LPDWORD)ctype);
    static const char* xmsg="<HTML><HEAD><TITLE>Shibboleth Error</TITLE></HEAD><BODY><H1>Shibboleth Error</H1>";
    DWORD resplen=strlen(xmsg);
    lpECB->WriteClient(lpECB->ConnID,(LPVOID)xmsg,&resplen,HSE_IO_SYNC);
    resplen=strlen(msg);
    lpECB->WriteClient(lpECB->ConnID,(LPVOID)msg,&resplen,HSE_IO_SYNC);
    static const char* xmsg2="</BODY></HTML>";
    resplen=strlen(xmsg2);
    lpECB->WriteClient(lpECB->ConnID,(LPVOID)xmsg2,&resplen,HSE_IO_SYNC);
    return HSE_STATUS_SUCCESS;
}

DWORD WriteClientError(LPEXTENSION_CONTROL_BLOCK lpECB, const char* filename, ShibMLP& mlp)
{
    ifstream infile(filename);
    if (!infile)
        return WriteClientError(lpECB,"Unable to open error template, check settings.");   

    string res = mlp.run(infile);
    static const char* ctype="Content-Type: text/html\r\n";
    lpECB->ServerSupportFunction(lpECB->ConnID,HSE_REQ_SEND_RESPONSE_HEADER,"200 OK",0,(LPDWORD)ctype);
    DWORD resplen=res.length();
    lpECB->WriteClient(lpECB->ConnID,(LPVOID)res.c_str(),&resplen,0);
    return HSE_STATUS_SUCCESS;
}

extern "C" DWORD WINAPI HttpExtensionProc(LPEXTENSION_CONTROL_BLOCK lpECB)
{
    ostringstream threadid;
    threadid << "[" << getpid() << "] shire" << '\0';
    saml::NDC ndc(threadid.str().c_str());

    ShibINI& ini = g_Config->getINI();
    string shireError;
    ShibMLP markupProcessor;

    try
    {
        // Determine web site number. This can't really fail, I don't think.
        dynabuf buf(128);
        ULONG site_id=0;
        GetServerVariable(lpECB,"INSTANCE_ID",buf,10);
        if ((site_id=strtoul(buf,NULL,10))==0)
            return WriteClientError(lpECB,"IIS site instance appears to be invalid.");

        // Match site instance to site settings.
        if (site_id>g_Sites.size() || g_Sites[site_id-1].length()==0)
            return WriteClientError(lpECB,"Shibboleth filter not configured for this web site.");
        string& site=g_Sites[site_id-1];

        string target_url,application_id;
        get_target_and_appid(lpECB,site.c_str(),target_url,application_id);

        if (!ini.get_tag(application_id, "shireError", true, &shireError))
            return WriteClientError(lpECB,"The shireError configuration setting is missing, check configuration.");

        string shire_url = target_url;

        // Set SHIRE policies.
        SHIREConfig config;
        string tag;
        config.checkIPAddress = (ini.get_tag(application_id,"checkIPAddress",true,&tag) && ShibINI::boolean(tag));
        config.lifetime=config.timeout=0;
        tag.erase();
        if (ini.get_tag(application_id, "authLifetime", true, &tag))
            config.lifetime=strtoul(tag.c_str(),NULL,10);
        tag.erase();
        if (ini.get_tag(application_id, "authTimeout", true, &tag))
            config.timeout=strtoul(tag.c_str(),NULL,10);

        // Pull the config data we need to handle the various possible conditions.
        string shib_cookie;
        if (!ini.get_tag(application_id, "cookieName", true, &shib_cookie))
            return WriteClientError(lpECB,"The cookieName configuration setting is missing, check configuration.");
    
        string wayfLocation;
        if (!ini.get_tag(application_id, "wayfURL", true, &wayfLocation))
            return WriteClientError(lpECB,"The wayfURL configuration setting is missing, check configuration.");
    
        bool has_tag = ini.get_tag(application_id, "supportContact", true, &tag);
        markupProcessor.insert("supportContact", has_tag ? tag : "");
        has_tag = ini.get_tag(application_id, "logoLocation", true, &tag);
        markupProcessor.insert("logoLocation", has_tag ? tag : "");
        markupProcessor.insert("requestURL", target_url.c_str());
  
        SHIRE shire(config, shire_url.c_str());

        // Process SHIRE POST
        if (ini.get_tag(application_id, "shireSSLOnly", true, &tag) && ShibINI::boolean(tag))
        {
            // Make sure this is SSL, if it should be.
            GetServerVariable(lpECB,"HTTPS",buf,10);
            if (buf!="on")
                throw ShibTargetException(SHIBRPC_OK,"blocked non-SSL access to SHIRE POST processor");
        }
        
        // Make sure this is a POST
        if (stricmp(lpECB->lpszMethod,"POST"))
            throw ShibTargetException(SHIBRPC_OK,"blocked non-POST to SHIRE POST processor");

        // Sure sure this POST is an appropriate content type
        if (!lpECB->lpszContentType || stricmp(lpECB->lpszContentType,"application/x-www-form-urlencoded"))
            throw ShibTargetException(SHIBRPC_OK,"blocked bad content-type to SHIRE POST processor");
    
        // Make sure the "bytes sent" is a reasonable number and that we have all of it.
        if (lpECB->cbTotalBytes > 1024*1024) // 1MB?
            throw ShibTargetException (SHIBRPC_OK,"blocked too-large a post to SHIRE POST processor");
        else if (lpECB->cbTotalBytes>lpECB->cbAvailable)
            throw ShibTargetException (SHIBRPC_OK,"blocked incomplete post to SHIRE POST processor");

        // Parse the incoming data.
        HQUERY params=ParseQuery(lpECB);
        if (!params)
            throw ShibTargetException (SHIBRPC_OK,"unable to parse form data");

        // Make sure the TARGET parameter exists
        const char* target = QueryValue(params,"TARGET");
        if (!target || *target == '\0')
            throw ShibTargetException(SHIBRPC_OK,"SHIRE POST failed to find TARGET parameter");
    
        // Make sure the SAMLResponse parameter exists
        const char* post = QueryValue(params,"SAMLResponse");
        if (!post || *post == '\0')
            throw ShibTargetException (SHIBRPC_OK,"SHIRE POST failed to find SAMLResponse parameter");

        GetServerVariable(lpECB,"REMOTE_ADDR",buf,16);

        // Process the post.
        string cookie;
        RPCError* status = shire.sessionCreate(post,buf,application_id.c_str(),cookie);
    
        if (status->isError()) {
            if (status->isRetryable()) {
                delete status;
                char timebuf[16];
                sprintf(timebuf,"%u",time(NULL));
                string wayf=wayfLocation + "?shire=" + url_encode(shire_url.c_str()) + "&target=" + url_encode(target) +
                    "&time=" + timebuf + "&providerId=" + application_id;
                DWORD len=wayf.length();
                if (lpECB->ServerSupportFunction(lpECB->ConnID,HSE_REQ_SEND_URL_REDIRECT_RESP,(LPVOID)wayf.c_str(),&len,0))
                    return HSE_STATUS_SUCCESS;
                return HSE_STATUS_ERROR;
            }
    
            // Return this error to the user.
            markupProcessor.insert(*status);
            delete status;
            return WriteClientError(lpECB,shireError.c_str(),markupProcessor);
        }
        delete status;
    
        // We've got a good session, set the cookie and redirect to target.
        shib_cookie = "Set-Cookie: " + shib_cookie + '=' + cookie + "; path=/\r\n" 
            "Location: " + target + "\r\n"
            "Expires: 01-Jan-1997 12:00:00 GMT\r\n"
            "Cache-Control: private,no-store,no-cache\r\n"
            "Connection: close\r\n";
        HSE_SEND_HEADER_EX_INFO hinfo;
        hinfo.pszStatus="302 Moved";
        hinfo.pszHeader=shib_cookie.c_str();
        hinfo.cchStatus=9;
        hinfo.cchHeader=shib_cookie.length();
        hinfo.fKeepConn=FALSE;
        if (lpECB->ServerSupportFunction(lpECB->ConnID,HSE_REQ_SEND_RESPONSE_HEADER_EX,&hinfo,0,0))
            return HSE_STATUS_SUCCESS;
    }
    catch (ShibTargetException &e) {
        markupProcessor.insert ("errorType", "SHIRE Processing Error");
        markupProcessor.insert ("errorText", e.what());
        markupProcessor.insert ("errorDesc", "An error occurred while processing your request.");
        return WriteClientError(lpECB,shireError.c_str(),markupProcessor);
    }
    catch (...) {
        markupProcessor.insert ("errorType", "SHIRE Processing Error");
        markupProcessor.insert ("errorText", "Unexpected Exception");
        markupProcessor.insert ("errorDesc", "An error occurred while processing your request.");
        return WriteClientError(lpECB,shireError.c_str(),markupProcessor);
    }
    
    return HSE_STATUS_ERROR;
}

