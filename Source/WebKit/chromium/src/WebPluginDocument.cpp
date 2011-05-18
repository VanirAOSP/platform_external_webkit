/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebPluginDocument.h"

#include "Document.h"
#include "PluginDocument.h"

#include "WebPluginContainerImpl.h"

#include <wtf/PassRefPtr.h>

using namespace WebCore;

namespace WebKit {


WebPlugin* WebPluginDocument::plugin()
{
    if (!isPluginDocument())
        return 0;
    PluginDocument* doc = unwrap<PluginDocument>();
    WebPluginContainerImpl* container = static_cast<WebPluginContainerImpl*>(static_cast<PluginDocument*>(doc)->pluginWidget());
    return container->plugin();
}


WebPluginDocument::WebPluginDocument(const PassRefPtr<PluginDocument>& elem)
    : WebDocument(elem)
{
}

WebPluginDocument& WebPluginDocument::operator=(const PassRefPtr<PluginDocument>& elem)
{
    m_private = elem;
    return *this;
}

WebPluginDocument::operator PassRefPtr<PluginDocument>() const
{
    return static_cast<PluginDocument*>(m_private.get());
}

} // namespace WebKit