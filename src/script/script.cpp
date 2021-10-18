#include "script.hpp"

#include <boost/algorithm/string.hpp>
#include <deque>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

#include "json_reader.hpp"
#include "script_reader.hpp"

namespace traffic
{
script::script(const std::string& path)
{
    std::ifstream json_file(path);
    if (!json_file)
    {
        throw std::logic_error("File " + path +
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
    // VALIDATE SAVE in MESSAGES
    // start building a set<string>. If insert fails -> boom

    // VALIDATE LOAD in MESSAGES
    // use the set<string>. If load not found: boom
    for (const auto& [k, _] : vars)
    {
        if (ranges.find(k) != ranges.end())
        {
            throw std::logic_error(
                k + " found in both ranges and variables. Please, choose a different name.");
        }
    }

    for (const auto& m : messages)
    {
        for (const std::string& forbidden : {"content_type", "content_length"})
        {
            if (m.headers.find(forbidden) != m.headers.end())
            {
                throw std::logic_error(
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

const std::vector<std::string> script::get_message_names() const
{
    std::vector<std::string> res;
    for (const auto& m : messages)
    {
        res.push_back(m.id);
    }

    return res;
}

bool script::save_from_answer(const std::string& answer, const msg_modifier& sfa)
{
    try
    {
        json_reader ans_json{answer, "{}"};
        if (sfa.value_type == "string")
        {
            saved_strs[sfa.name] = ans_json.get_value<std::string>(sfa.path);
        }
        else if (sfa.value_type == "int")
        {
            saved_ints[sfa.name] = ans_json.get_value<int>(sfa.path);
        }
        else if (sfa.value_type == "object")
        {
            saved_jsons[sfa.name] = ans_json.get_value<json_reader>(sfa.path);
        }
    }
    catch (std::logic_error& le)
    {
        return false;
    }
    return true;
}

bool script::add_to_request(const msg_modifier& atb, message& m)
{
    json_reader modified_body(m.body, "{}");

    try
    {
        if (atb.value_type == "string")
        {
            modified_body.set(atb.path, saved_strs.at(atb.name));
        }
        else if (atb.value_type == "int")
        {
            modified_body.set(atb.path, saved_ints.at(atb.name));
        }
        else if (atb.value_type == "object")
        {
            modified_body.set(atb.path, saved_jsons.at(atb.name));
        }
        m.body = modified_body.as_string();
    }
    catch (std::out_of_range& oor)
    {
        return false;
    }
    return true;
}

bool script::process_next(const answer_type& last_answer)
{
    // TODO: if this is an error, validation should fail. Rethink
    const auto& last_msg = messages.front();
    if (last_msg.sfa.has_value())
    {
        if (!save_from_answer(last_answer.body, *last_msg.sfa))
        {
            return false;
        }
    }

    messages.pop_front();

    auto& next_msg = messages.front();
    if (next_msg.atb.has_value())
    {
        if (!add_to_request(*next_msg.atb, next_msg))
        {
            return false;
        }
    }

    return true;
}

bool script::validate_answer(const answer_type& last_answer) const
{
    return last_answer.result_code == messages.front().pass_code;
}

const bool script::post_process(const answer_type& last_answer)
{
    return !is_last() && process_next(last_answer);
}

void script::replace_in_messages(const std::string& old_str, const std::string& new_str)
{
    for (auto& m : messages)
    {
        std::string str_to_replace = "<" + old_str + ">";
        boost::replace_all(m.body, str_to_replace, new_str);
        boost::replace_all(m.url, str_to_replace, new_str);

        traffic::msg_headers new_headers;
        for (std::pair<std::string, std::string> p : m.headers)
        {
            boost::replace_all(p.first, str_to_replace, new_str);
            boost::replace_all(p.second, str_to_replace, new_str);
            new_headers.emplace(p);
        }
        m.headers = std::move(new_headers);
    }
}

void script::parse_ranges(const std::map<std::string, int64_t>& current)
{
    for (const auto& c : current)
    {
        replace_in_messages(c.first, std::to_string(c.second));
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
