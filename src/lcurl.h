#pragma once

#include <curl/curl.h>
#include "lua_kit.h"

using namespace std;

namespace lcurl {

    static size_t write_callback(char* buffer, size_t block_size, size_t count, void* arg);

    class curl_request
    {
    public:
        curl_request(CURLM* cm, CURL* c) : curlm(cm), curl(c) {}
        ~curl_request() {
            if (curl) {
                curl_multi_remove_handle(curlm, curl);
                curl = nullptr;
            }
            if (header) {
                curl_slist_free_all(header);
                header = nullptr;
            }
            curlm = nullptr;
        }

        bool create(const string& url, size_t timeout_ms) {
            CURLcode ret_result;
            ret_result = curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            ret_result = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
            ret_result = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)this);
            ret_result = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
            ret_result = curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
            ret_result = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms / 2);
            ret_result = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            ret_result = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
            ret_result = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);
            ret_result = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            return true;
        }

        bool call_get(string data) {
            return request(data);
        }

        bool call_post(string data) {
            CURLcode ret_result = curl_easy_setopt(curl, CURLOPT_POST, 1L);
            return request(data);
        }

        bool call_put(string data) {
            CURLcode ret_result = curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            return request(data);
        }

        bool call_del(string data) {
            CURLcode ret_result = curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            return request(data);
        }

        void set_header(string value) {
            header = curl_slist_append(header, value.c_str());
        }

        luakit::variadic_results get_respond(lua_State* L) {
            long code = 0;
            luakit::kit_state kit_state(L);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            if (error[0] == '\0') {
                return kit_state.as_return(content, code);
            }
            return kit_state.as_return(nullptr, code, error);
        }

    private:
        bool request(const string& data) {
            if (header) {
                CURLcode ret_result = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header);
            }
            if (data.size() > 0) {
                CURLcode ret_result = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
                CURLcode ret_result2 = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());
            }
            if (curl_multi_add_handle(curlm, curl) == CURLM_OK) {
                return true;
            }
            return false;
        }

    public:
        string content;

    private:
        CURL* curl = nullptr;
        CURLM* curlm = nullptr;
        curl_slist* header = nullptr;
        char error[CURL_ERROR_SIZE] = {};
    };

    class curlm_mgr
    {
    public:
        curlm_mgr(CURLM* cm, CURL* ce) : curlm(cm), curle(ce) {}

        void destory() {
            if (curle) {
                curl_easy_cleanup(curle);
                curle = nullptr;
            }
            if (curlm) {
                curl_multi_cleanup(curlm);
                curlm = nullptr;
            }
            curl_global_cleanup();
        }

        luakit::variadic_results create_request(lua_State* L, string url, size_t timeout_ms) {
            CURL* curl = curl_easy_init();
            if (!curl) {
                return luakit::variadic_results();
            }
            curl_request* request = new curl_request(curlm, curl);
            request->create(url, timeout_ms);
            luakit::kit_state kit_state(L);
            return kit_state.as_return(request, curl);
        }

        int update(lua_State* L) {
            luakit::kit_state kit_state(L);
            while (true) {
                int msgs_in_queue;
                CURLMsg* curlmsg = curl_multi_info_read(curlm, &msgs_in_queue);
                if (!curlmsg) {
                    int running_handles;
                    CURLMcode perform_result = curl_multi_perform(curlm, &running_handles);
                    if (perform_result != CURLM_OK && perform_result != CURLM_CALL_MULTI_PERFORM) {
                        lua_pushboolean(L, false);
                        lua_pushstring(L, "curl_multi_perform failed");
                        return 2;
                    }
                    break;
                }
                if (curlmsg->msg == CURLMSG_DONE){
                    CURLcode ret_result = curlmsg->data.result;
                    kit_state.object_call(this, "on_respond", nullptr, std::tie(), curlmsg->easy_handle, ret_result);
                }
            }
            lua_pushboolean(L, true);
            return 1;
        }

    private:
        CURL* curle = nullptr;
        CURLM* curlm = nullptr;
    };

    static size_t write_callback(char* buffer, size_t block_size, size_t count, void* arg) {
        size_t length = block_size * count;
        curl_request* request = (curl_request*)arg;
        if (request) {
            request->content.append(buffer, length);
        }
        return length;
    }

}
