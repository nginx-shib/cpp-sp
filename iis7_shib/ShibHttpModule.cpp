/**
* Licensed to the University Corporation for Advanced Internet
* Development, Inc. (UCAID) under one or more contributor license
* agreements. See the NOTICE file distributed with this work for
* additional information regarding copyright ownership.
*
* UCAID licenses this file to you under the Apache License,
* Version 2.0 (the "License"); you may not use this file except
* in compliance with the License. You may obtain a copy of the
* License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an
* "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
* either express or implied. See the License for the specific
* language governing permissions and limitations under the License.
*/

#include "IIS7_shib.hpp"

#include <process.h>

#include <xmltooling/util/NDC.h>
#include "ShibHttpModule.hpp"
#include "NativeRequest.hpp"

using namespace Config;

REQUEST_NOTIFICATION_STATUS
ShibHttpModule::DoHandler(
    _In_ IHttpContext *         pHttpContext,
    _In_ IHttpEventProvider *   pProvider
)
{
    string threadid("[");
    threadid += lexical_cast<string>(_getpid()) + "] native_shib";
    xmltooling::NDC ndc(threadid.c_str());

    // TODO the handler and the Filter both use the same class.  Should it?
    NativeRequest handler(pHttpContext, pProvider, false);

    pair<bool, long> res = handler.getServiceProvider().doHandler(handler);

    if (res.first) {
        return static_cast<REQUEST_NOTIFICATION_STATUS>(res.second);
    }
    return RQ_NOTIFICATION_CONTINUE;
}

REQUEST_NOTIFICATION_STATUS
ShibHttpModule::DoFilter(
    _In_ IHttpContext *             pHttpContext,
    _In_ IHttpEventProvider *  pProvider
)
{
    const IHttpRequest *req(pHttpContext->GetRequest());

    string threadid("[");
    threadid += lexical_cast<string>(_getpid()) + "] native_shib";
    xmltooling::NDC ndc(threadid.c_str());

    // TODO Different class?
    NativeRequest filter(pHttpContext, pProvider, true);

    pair<bool, long> res = filter.getServiceProvider().doAuthentication(filter);
    if (res.first) {
        return static_cast<REQUEST_NOTIFICATION_STATUS>(res.second);
    }

    if (!g_spoofKey.empty() && filter.isUseHeaders()) {
        const HRESULT hr(pHttpContext->GetRequest()->SetHeader(SpoofHeaderName, g_spoofKey.c_str(), static_cast<USHORT>(g_spoofKey.length()), TRUE));
        if (FAILED(hr)) {
            (void)pHttpContext->GetResponse()->SetStatus(static_cast<USHORT>(filter.XMLTOOLING_HTTP_STATUS_ERROR), "Fatal Server Error", 0, hr);
            return RQ_NOTIFICATION_FINISH_REQUEST;
        }
    }
    res = filter.getServiceProvider().doExport(filter);
    if (res.first) {
        return static_cast<REQUEST_NOTIFICATION_STATUS>(res.second);
    }

    res = filter.getServiceProvider().doAuthorization(filter);
    if (res.first) {
        return static_cast<REQUEST_NOTIFICATION_STATUS>(res.second);
    }
    return RQ_NOTIFICATION_CONTINUE;
}

// RQ_BEGIN_REQUEST
REQUEST_NOTIFICATION_STATUS
ShibHttpModule::OnBeginRequest(
    _In_ IHttpContext *         pHttpContext,
    _In_ IHttpEventProvider *   pProvider
)
{
    return DoHandler(pHttpContext, pProvider);
}

// RQ_AUTHENTICATE_REQUEST 
REQUEST_NOTIFICATION_STATUS
ShibHttpModule::OnAuthenticateRequest(
    _In_ IHttpContext *             pHttpContext,
    _In_ IAuthenticationProvider *  pProvider
)
{
    return DoFilter(pHttpContext, pProvider);
}

// RQ_AUTHENTICATE_REQUEST 
REQUEST_NOTIFICATION_STATUS
ShibHttpModule::OnPostAuthenticateRequest(
    _In_ IHttpContext *             pHttpContext,
    _In_ IHttpEventProvider *       pProvider
)
{
    return RQ_NOTIFICATION_CONTINUE;
}

// RQ_PRE_EXECUTE_REQUEST_HANDLER
REQUEST_NOTIFICATION_STATUS
ShibHttpModule::OnPreExecuteRequestHandler(
    _In_ IHttpContext *         pHttpContext,
    _In_ IHttpEventProvider *   pProvider
)
{
    return RQ_NOTIFICATION_CONTINUE;
}

