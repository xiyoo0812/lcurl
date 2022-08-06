# lcurl
一个封装curl的lua扩展库！

# 依赖
- curl
- [lua](https://github.com/xiyoo0812/lua.git)5.3以上
- [luakit](https://github.com/xiyoo0812/luakit.git)一个luabind库
- 项目路径如下<br>
  |--proj <br>
  &emsp;|--lua <br>
  &emsp;|--lcurl <br>
  &emsp;|--luakit

# 注意事项
- linux编译请先安装curl-devel。

# 用法
```lua
--本示例使用了quanta引擎
--https://github.com/xiyoo0812/quanta.git
--httpClient.lua
local lcurl = require("lcurl")
local ljson = require("lcjson")

local pairs         = pairs
local log_err       = logger.err
local tconcat       = table.concat
local sformat       = string.format
local qxpcall       = quanta.xpcall
local jencode       = ljson.encode
local luencode      = lcurl.url_encode

local curlm_mgr     = lcurl.curlm_mgr
local thread_mgr    = quanta.get("thread_mgr")
local update_mgr    = quanta.get("update_mgr")

local HTTP_CALL_TIMEOUT = quanta.enum("NetwkTime", "HTTP_CALL_TIMEOUT")

local HttpClient = singleton()
local prop = property(HttpClient)
prop:reader("contexts", {})

function HttpClient:__init()
    --加入帧更新
    update_mgr:attach_frame(self)
    --退出通知
    update_mgr:attach_quit(self)
    --创建管理器
    curlm_mgr.on_respond = function(curl_handle, result)
        qxpcall(self.on_respond, "on_respond: %s", self, curl_handle, result)
    end
end

function HttpClient:on_quit()
    self.contexts = {}
    curlm_mgr.destory()
end

function HttpClient:on_frame()
    if next(self.contexts) then
        curlm_mgr.update()
        --清除超时请求
        local clock_ms = quanta.clock_ms
        for handle, context in pairs(self.contexts) do
            if clock_ms >= context.time then
                self.contexts[handle] = nil
            end
        end
    end
end

function HttpClient:on_respond(curl_handle, result)
    local context = self.contexts[curl_handle]
    if context then
        local request = context.request
        local session_id = context.session_id
        local content, code, err = request.get_respond()
        if result == 0 then
            thread_mgr:response(session_id, true, code, content)
        else
            thread_mgr:response(session_id, false, code, err)
        end
        self.contexts[curl_handle] = nil
    end
end

function HttpClient:format_url(url, query)
    if query then
        local qtype = type(query)
        if qtype == "string" and #query > 0 then
            return sformat("%s?%s", url, query)
        end
        if qtype == "table" and next(query) then
            local fquery = {}
            for key, value in pairs(query) do
                fquery[#fquery + 1] = sformat("%s=%s", luencode(key), luencode(value))
            end
            return sformat("%s?%s", url, tconcat(fquery, "&"))
        end
    end
    return url
end

--构建请求
function HttpClient:send_request(url, timeout, querys, headers, method, datas)
    local to = timeout or HTTP_CALL_TIMEOUT
    local fmt_url = self:format_url(url, querys)
    local request, curl_handle = curlm_mgr.create_request(fmt_url, to)
    if not request then
        log_err("[HttpClient][send_request] failed : %s", curl_handle)
        return false
    end
    if not headers then
        headers = {["Content-Type"] = "text/plain" }
    end
    if type(datas) == "table" then
        datas = jencode(datas)
        headers["Content-Type"] = "application/json"
    end
    for key, value in pairs(headers or {}) do
        request.set_header(sformat("%s:%s", key, value))
    end
    local ok, err = request[method](datas or "")
    if not ok then
        log_err("[HttpClient][send_request] curl %s failed: %s!", method, err)
        return false
    end
    local session_id = thread_mgr:build_session_id()
    self.contexts[curl_handle] = {
        request = request,
        session_id = session_id,
        time = quanta.clock_ms + to,
    }
    return thread_mgr:yield(session_id, url, to)
end

--get接口
function HttpClient:call_get(url, querys, headers, datas, timeout)
    return self:send_request(url, timeout, querys, headers, "call_get", datas)
end

--post接口
function HttpClient:call_post(url, datas, headers, querys, timeout)
    return self:send_request(url, timeout, querys, headers, "call_post", datas)
end

--put接口
function HttpClient:call_put(url, datas, headers, querys, timeout)
    return self:send_request(url, timeout, querys, headers, "call_put", datas)
end

--del接口
function HttpClient:call_del(url, querys, headers, timeout)
    return self:send_request(url, timeout, querys, headers, "call_put")
end

quanta.http_client = HttpClient()

return HttpClient

```
