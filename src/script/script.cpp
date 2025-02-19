#include "script.hpp"

#include <boost/algorithm/string.hpp>
#include <deque>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "json_reader.hpp"
#include "script_functions.hpp"
#include "script_reader.hpp"

namespace traffic
{
script::script(const std::string& path)
{
    std::ifstream json_file(path);
    if (!json_file)
    {
        throw std::out_of_range("File " + path +
                                " not found."
                                "Terminating application.");
    }
    const auto json_str =
        std::string((std::istreambuf_iterator<char>(json_file)), std::istreambuf_iterator<char>());

    build(json_str);
}

script::script(const json_reader& input_json)
{
    build(input_json.as_string());
}

void script::validate_members() const
{
    std::set<std::string, std::less<>> unique_ids;
    check_repeated(unique_ids, vars);
    check_repeated(unique_ids, ranges);

    for (const auto& m : messages)
    {
        for (const std::string& forbidden : {"content_type", "content_length"})
        {
            if (m.headers.find(forbidden) != m.headers.end())
            {
                throw std::invalid_argument(
                    forbidden + " is built automatically in headers. Cannot set custom values.");
            }
        }
    }
}

void script::build(const std::string& input_json)
{
    script_reader sr{input_json};
    ranges = sr.build_ranges();
    messages = sr.build_messages();
    server = sr.build_server_info();
    timeout_ms = sr.build_timeout();
    vars = sr.build_variables();
    validate_members();
}

std::vector<std::string> script::get_message_names() const
{
    std::vector<std::string> res;
    for (const auto& m : messages)
    {
        res.push_back(m.id);
    }

    return res;
}

bool script::save_from_answer(const answer_type& answer, const msg_modifier& sfa)
{
    try
    {
        save_headers(sfa.headers, answer.headers, vars);

        for (const auto& [id, mm] : sfa.body_fields)
        {
            json_reader ans_json{answer.body, "{}"};
            if (mm.value_type == "string")
            {
                saved_strs[id] = ans_json.get_value<std::string>(mm.path);
            }
            else if (mm.value_type == "int")
            {
                saved_ints[id] = ans_json.get_value<int>(mm.path);
            }
            else if (mm.value_type == "object")
            {
                saved_jsons[id] = ans_json.get_value<json_reader>(mm.path);
            }
        }
    }
    catch (const std::logic_error&)
    {
        return false;
    }
    return true;
}

bool script::add_to_request(const std::map<std::string, body_modifier, std::less<>>& atb,
                            message& m)
{
    json_reader modified_body(m.body, "{}");

    try
    {
        for (const auto& [id, mm] : atb)
        {
            if (mm.value_type == "string")
            {
                modified_body.set(mm.path, saved_strs.at(id));
            }
            else if (mm.value_type == "int")
            {
                modified_body.set(mm.path, saved_ints.at(id));
            }
            else if (mm.value_type == "object")
            {
                modified_body.set(mm.path, saved_jsons.at(id));
            }
        }

        m.body = modified_body.as_string();
    }
    catch (const std::out_of_range&)
    {
        return false;
    }

    return true;
}

void replace_in_message(const std::string& old_str, const std::string& new_str, message& m)
{
    std::string str_to_replace = "<" + old_str + ">";
    boost::replace_all(m.body, str_to_replace, new_str);
    boost::replace_all(m.url, str_to_replace, new_str);

    traffic::msg_headers new_headers;
    for (std::pair<std::string, std::string> p : m.headers)
    {
        boost::replace_all(p.first, str_to_replace, new_str);
        boost::replace_all(p.second, str_to_replace, new_str);
        new_headers.insert(p);
    }
    m.headers = std::move(new_headers);
}

bool script::process_next(const answer_type& last_answer)
{
    // TODO: if this is an error, validation should fail. Rethink
    if (!save_from_answer(last_answer, messages.front().sfa))
    {
        return false;
    }

    messages.pop_front();

    auto& next_msg = messages.front();
    if (!add_to_request(next_msg.atb, next_msg))
    {
        return false;
    }

    for (const auto& [k, v] : vars)
    {
        replace_in_message(k, v, next_msg);
    }

    return true;
}

bool script::validate_answer(const answer_type& last_answer) const
{
    return last_answer.result_code == messages.front().pass_code;
}

bool script::post_process(const answer_type& last_answer)
{
    return !is_last() && process_next(last_answer);
}

void script::replace_in_messages(const std::string& old_str, const std::string& new_str)
{
    for (auto& m : messages)
    {
        replace_in_message(old_str, new_str, m);
    }
}

void script::parse_ranges(const std::map<std::string, int64_t, std::less<>>& current)
{
    for (const auto& [k, v] : current)
    {
        replace_in_messages(k, std::to_string(v));
    }
}

void script::parse_variables()
{
    for (const auto& [k, v] : vars)
    {
        replace_in_messages(k, v);
    }
}
}  // namespace traffic
