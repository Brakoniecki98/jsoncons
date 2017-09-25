// Copyright 2015 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_JSON_PARSER_HPP
#define JSONCONS_JSON_PARSER_HPP

#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <istream>
#include <cstdlib>
#include <stdexcept>
#include <system_error>
#include <jsoncons/json_exception.hpp>
#include <jsoncons/json_input_handler.hpp>
#include <jsoncons/parse_error_handler.hpp>
#include <jsoncons/json_error_category.hpp>

#define JSONCONS_ILLEGAL_CONTROL_CHARACTER \
        case 0x00:case 0x01:case 0x02:case 0x03:case 0x04:case 0x05:case 0x06:case 0x07:case 0x08:case 0x0b: \
        case 0x0c:case 0x0e:case 0x0f:case 0x10:case 0x11:case 0x12:case 0x13:case 0x14:case 0x15:case 0x16: \
        case 0x17:case 0x18:case 0x19:case 0x1a:case 0x1b:case 0x1c:case 0x1d:case 0x1e:case 0x1f 

namespace jsoncons {

// try_string_to_uinteger

template<class CharT> inline
bool try_string_to_uinteger(const CharT *s, size_t length, uint64_t& result)
{
    static const uint64_t max_value = (std::numeric_limits<uint64_t>::max)();
    static const uint64_t max_value_div_10 = max_value / 10;
    uint64_t n = 0;
    for (size_t i = 0; i < length; ++i)
    {
        uint64_t x = s[i] - '0';
        if (n > max_value_div_10)
        {
            return false;
        }
        n = n * 10;
        if (n > max_value - x)
        {
            return false;
        }

        n += x;
    }
    result = n;
    return true;
}

template<class CharT> inline
bool try_string_to_integer(bool has_neg, const CharT *s, size_t length, int64_t& result)
{
    if (has_neg)
    {
        static const int64_t min_value = (std::numeric_limits<int64_t>::min)();
        static const int64_t min_value_div_10 = min_value / 10;

        int64_t n = 0;
        const CharT* end = s+length; 
        for (const CharT* p = s; p < end; ++p)
        {
            int64_t x = *p - '0';
            if (n < min_value_div_10)
            {
                return false;
            }
            n = n * 10;
            if (n < min_value + x)
            {
                return false;
            }

            n -= x;
        }
        result = n;
        return true;
    }
    else
    {
        static const int64_t max_value = (std::numeric_limits<int64_t>::max)();
        static const int64_t max_value_div_10 = max_value / 10;

        int64_t n = 0;
        const CharT* end = s+length; 
        for (const CharT* p = s; p < end; ++p)
        {
            int64_t x = *p - '0';
            if (n > max_value_div_10)
            {
                return false;
            }
            n = n * 10;
            if (n > max_value - x)
            {
                return false;
            }

            n += x;
        }
        result = n;
        return true;
    }
}

enum class parse_state : uint8_t 
{
    root,
    start, 
    slash,  
    slash_slash, 
    slash_star, 
    slash_star_star,
    expect_comma_or_end,  
    object,
    expect_member_name_or_end, 
    expect_member_name, 
    expect_colon,
    expect_value_or_end,
    expect_value,
    array, 
    string_u1,
    member_name,
    escape, 
    escape_u1, 
    escape_u2, 
    escape_u3, 
    escape_u4, 
    escape_expect_surrogate_pair1, 
    escape_expect_surrogate_pair2, 
    escape_u6, 
    escape_u7, 
    escape_u8, 
    escape_u9, 
    minus, 
    zero,  
    integer,
    fraction1,
    fraction2,
    exp1,
    exp2,
    exp3,
    n,
    nu,
    nul,
    t,  
    tr,  
    tru,  
    f,  
    fa,  
    fal,  
    fals,  
    cr,
    lf,
    done
};

template <class CharT>
class basic_json_parser : private parsing_context
{
    static const int default_initial_stack_capacity_ = 100;
    typedef typename basic_json_input_handler<CharT>::string_view_type string_view_type;

    basic_null_json_input_handler<CharT> default_input_handler_;
    default_parse_error_handler default_err_handler_;

    basic_json_input_handler<CharT>& handler_;
    parse_error_handler& err_handler_;
    uint32_t cp_;
    uint32_t cp2_;
    std::basic_string<CharT> string_buffer_;
    bool is_negative_;

    size_t line_;
    size_t column_;
    int nesting_depth_;
    int initial_stack_capacity_;

    int max_depth_;
    string_to_double<CharT> str_to_double_;
    uint8_t precision_;
    const CharT* begin_input_;
    const CharT* end_input_;
    const CharT* p_;

    parse_state state_;
    std::vector<parse_state> state_stack_;

    // Noncopyable and nonmoveable
    basic_json_parser(const basic_json_parser&) = delete;
    basic_json_parser& operator=(const basic_json_parser&) = delete;

public:

    basic_json_parser()
       : handler_(default_input_handler_),
         err_handler_(default_err_handler_),
         cp_(0),
         cp2_(0),
         is_negative_(false),
         line_(1),
         column_(1),
         nesting_depth_(0), 
         initial_stack_capacity_(default_initial_stack_capacity_),
         precision_(0), 
         begin_input_(nullptr),
         end_input_(nullptr),
         p_(nullptr),
         state_(parse_state::start)
    {
        max_depth_ = (std::numeric_limits<int>::max)();

        state_stack_.reserve(initial_stack_capacity_);
        push_state(parse_state::root);
    }

    basic_json_parser(parse_error_handler& err_handler)
       : handler_(default_input_handler_),
         err_handler_(err_handler),
         cp_(0),
         cp2_(0),
         is_negative_(false),
         line_(1),
         column_(1),
         nesting_depth_(0), 
         initial_stack_capacity_(default_initial_stack_capacity_),
         precision_(0), 
         begin_input_(nullptr),
         end_input_(nullptr),
         p_(nullptr),
         state_(parse_state::start)
    {
        max_depth_ = (std::numeric_limits<int>::max)();

        state_stack_.reserve(initial_stack_capacity_);
        push_state(parse_state::root);
    }

    basic_json_parser(basic_json_input_handler<CharT>& handler)
       : handler_(handler),
         err_handler_(default_err_handler_),
         cp_(0),
         cp2_(0),
         is_negative_(false),
         line_(1),
         column_(1),
         nesting_depth_(0), 
         initial_stack_capacity_(default_initial_stack_capacity_),
         precision_(0), 
         begin_input_(nullptr),
         end_input_(nullptr),
         p_(nullptr),
         state_(parse_state::start)
    {
        max_depth_ = (std::numeric_limits<int>::max)();

        state_stack_.reserve(initial_stack_capacity_);
        push_state(parse_state::root);
    }

    basic_json_parser(basic_json_input_handler<CharT>& handler,
                      parse_error_handler& err_handler)
       : handler_(handler),
         err_handler_(err_handler),
         cp_(0),
         cp2_(0),
         is_negative_(false),
         line_(1),
         column_(1),
         nesting_depth_(0), 
         initial_stack_capacity_(default_initial_stack_capacity_),
         precision_(0), 
         begin_input_(nullptr),
         end_input_(nullptr),
         p_(nullptr),
         state_(parse_state::start)
    {
        max_depth_ = (std::numeric_limits<int>::max)();

        state_stack_.reserve(initial_stack_capacity_);
        push_state(parse_state::root);
    }

    size_t line_number() const
    {
        return line_;
    }

    size_t column_number() const
    {
        return column_;
    }

    void set_column_number(size_t column)
    {
        column_ = column;
    }

    bool source_exhausted() const
    {
        return p_ == end_input_;
    }

    const parsing_context& parsing_context() const
    {
        return *this;
    }

    ~basic_json_parser()
    {
    }

    size_t max_nesting_depth() const
    {
        return static_cast<size_t>(max_depth_);
    }

    void max_nesting_depth(size_t max_nesting_depth)
    {
        max_depth_ = static_cast<int>((std::min)(max_nesting_depth,static_cast<size_t>((std::numeric_limits<int>::max)())));
    }

    parse_state parent() const
    {
        JSONCONS_ASSERT(state_stack_.size() >= 1);
        return state_stack_.back();
    }

    bool done() const
    {
        return state_ == parse_state::done;
    }

    void skip_whitespace()
    {
        const CharT* local_end_input = end_input_;
        for (;;) 
        {
            if (JSONCONS_UNLIKELY(p_ == local_end_input)) 
            {
                return;
            } 
            else if (*p_ == ' ' || *p_ == '\t') 
            {
                ++p_;
                ++column_;                     
            } 
            else 
            {
                return;
            }
        }
    }

    void do_begin_object(std::error_code& ec)
    {
        if (++nesting_depth_ >= max_depth_)
        {
            if (err_handler_.error(json_parser_errc::max_depth_exceeded, *this))
            {
                ec = json_parser_errc::max_depth_exceeded;
                return;
            }
        } 
        push_state(parse_state::object);
        state_ = parse_state::expect_member_name_or_end;
        handler_.begin_object(*this);
    }

    void do_end_object(std::error_code& ec)
    {
        --nesting_depth_;
        state_ = pop_state();
        if (state_ == parse_state::object)
        {
            handler_.end_object(*this);
        }
        else if (state_ == parse_state::array)
        {
            err_handler_.fatal_error(json_parser_errc::expected_comma_or_right_bracket, *this);
            ec = json_parser_errc::expected_comma_or_right_bracket;
            return;
        }
        else
        {
            err_handler_.fatal_error(json_parser_errc::unexpected_right_brace, *this);
            ec = json_parser_errc::unexpected_right_brace;
            return;
        }

        if (parent() == parse_state::root)
        {
            state_ = parse_state::done;
            handler_.end_json();
        }
        else
        {
            state_ = parse_state::expect_comma_or_end;
        }
    }

    void do_begin_array(std::error_code& ec)
    {
        if (++nesting_depth_ >= max_depth_)
        {
            if (err_handler_.error(json_parser_errc::max_depth_exceeded, *this))
            {
                ec = json_parser_errc::max_depth_exceeded;
                return;
            }

        }
        push_state(parse_state::array);
        state_ = parse_state::expect_value_or_end;
        handler_.begin_array(*this);
    }

    void do_end_array(std::error_code& ec)
    {
        --nesting_depth_;
        state_ = pop_state();
        if (state_ == parse_state::array)
        {
            handler_.end_array(*this);
        }
        else if (state_ == parse_state::object)
        {
            err_handler_.fatal_error(json_parser_errc::expected_comma_or_right_brace, *this);
            ec = json_parser_errc::expected_comma_or_right_brace;
            return;
        }
        else
        {
            err_handler_.fatal_error(json_parser_errc::unexpected_right_bracket, *this);
            ec = json_parser_errc::unexpected_right_bracket;
            return;
        }
        if (parent() == parse_state::root)
        {
            state_ = parse_state::done;
            handler_.end_json();
        }
        else
        {
            state_ = parse_state::expect_comma_or_end;
        }
    }

    void reset()
    {
        state_stack_.clear();
        state_stack_.reserve(initial_stack_capacity_);
        push_state(parse_state::root);
        state_ = parse_state::start;
        line_ = 1;
        column_ = 1;
        nesting_depth_ = 0;
    }

    void check_done()
    {
        std::error_code ec;
        check_done(ec);
        if (ec)
        {
            throw parse_error(ec,line_,column_);
        }
    }

    void check_done(std::error_code& ec)
    {
        if (state_ != parse_state::done)
        {
            if (err_handler_.error(json_parser_errc::unexpected_eof, *this))
            {
                ec = json_parser_errc::unexpected_eof;
                return;
            }
        }
        for (; p_ != end_input_; ++p_)
        {
            CharT curr_char_ = *p_;
            switch (curr_char_)
            {
            case '\n':
            case '\r':
            case '\t':
            case ' ':
                break;
            default:
                if (err_handler_.error(json_parser_errc::extra_character, *this))
                {
                    ec = json_parser_errc::extra_character;
                    return;
                }
                break;
            }
        }
    }

    void parse(std::error_code& ec)
    {
        const CharT* local_end_input = end_input_;

        while ((p_ < local_end_input) && (state_ != parse_state::done))
        {
            switch (state_)
            {
            case parse_state::cr:
                ++line_;
                column_ = 1;
                switch (*p_)
                {
                case '\n':
                    state_ = pop_state();
                    ++p_;
                    break;
                default:
                    state_ = pop_state();
                    break;
                }
                break;
            case parse_state::lf:
                ++line_;
                column_ = 1;
                state_ = pop_state();
                break;
            case parse_state::start: 
                {
                    handler_.begin_json();
                    switch (*p_)
                    {
                    JSONCONS_ILLEGAL_CONTROL_CHARACTER:
                        if (err_handler_.error(json_parser_errc::illegal_control_character, *this))
                        {
                            ec = json_parser_errc::illegal_control_character;
                            return;
                        }
                        break;
                    case '\r': 
                        push_state(state_);
                        ++p_;
                        ++column_;
                        state_ = parse_state::cr;
                        break; 
                    case '\n': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::lf;
                        break;   
                    case ' ':case '\t':
                        skip_whitespace();
                        break;
                    case '/': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::slash;
                        break;
                    case '{':
                        do_begin_object(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case '[':
                        do_begin_array(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case '\"':
                        state_ = parse_state::string_u1;
                        ++p_;
                        ++column_;
                        break;
                    case '-':
                        is_negative_ = true;
                        state_ = parse_state::minus;
                        ++p_;
                        ++column_;
                        break;
                    case '0': 
                        string_buffer_.push_back(*p_);
                        state_ = parse_state::zero;
                        ++p_;
                        ++column_;
                        break;
                    case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
                        string_buffer_.push_back(*p_);
                        state_ = parse_state::integer;
                        ++p_;
                        ++column_;
                        break;
                    case 'n':
                        parse_null(ec);
                        if (ec) {return;}
                        break;
                    case 't':
                        parse_true(ec);
                        if (ec) {return;}
                        break;
                    case 'f':
                        parse_false(ec);
                        if (ec) {return;}
                        break;
                    case '}':
                        err_handler_.fatal_error(json_parser_errc::unexpected_right_brace, *this);
                        ec = json_parser_errc::unexpected_right_brace;
                        return;
                    case ']':
                        err_handler_.fatal_error(json_parser_errc::unexpected_right_bracket, *this);
                        ec = json_parser_errc::unexpected_right_bracket;
                        return;
                    default:
                        err_handler_.fatal_error(json_parser_errc::invalid_json_text, *this);
                        ec = json_parser_errc::invalid_json_text;
                        return;
                    }
                }
                break;

            case parse_state::expect_comma_or_end: 
                {
                    switch (*p_)
                    {
                    JSONCONS_ILLEGAL_CONTROL_CHARACTER:
                        if (err_handler_.error(json_parser_errc::illegal_control_character, *this))
                        {
                            ec = json_parser_errc::illegal_control_character;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    case '\r': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::cr;
                        break; 
                    case '\n': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::lf;
                        break;   
                    case ' ':case '\t':
                        skip_whitespace();
                        break;
                    case '/':
                        ++p_;
                        ++column_;
                        push_state(state_); 
                        state_ = parse_state::slash;
                        break;
                    case '}':
                        do_end_object(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case ']':
                        do_end_array(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case ',':
                        begin_member_or_element(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    default:
                        if (parent() == parse_state::array)
                        {
                            if (err_handler_.error(json_parser_errc::expected_comma_or_right_bracket, *this))
                            {
                                ec = json_parser_errc::expected_comma_or_right_bracket;
                                return;
                            }
                        }
                        else if (parent() == parse_state::object)
                        {
                            if (err_handler_.error(json_parser_errc::expected_comma_or_right_brace, *this))
                            {
                                ec = json_parser_errc::expected_comma_or_right_brace;
                                return;
                            }
                        }
                        ++p_;
                        ++column_;
                        break;
                    }
                }
                break;
            case parse_state::expect_member_name_or_end: 
                {
                    switch (*p_)
                    {
                    JSONCONS_ILLEGAL_CONTROL_CHARACTER:
                        if (err_handler_.error(json_parser_errc::illegal_control_character, *this))
                        {
                            ec = json_parser_errc::illegal_control_character;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    case '\r': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::cr;
                        break; 
                    case '\n': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::lf;
                        break;   
                    case ' ':case '\t':
                        skip_whitespace();
                        break;
                    case '/':
                        ++p_;
                        ++column_;
                        push_state(state_); 
                        state_ = parse_state::slash;
                        break;
                    case '}':
                        do_end_object(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case '\"':
                        ++p_;
                        ++column_;
                        push_state(parse_state::member_name);
                        state_ = parse_state::string_u1;
                        break;
                    case '\'':
                        if (err_handler_.error(json_parser_errc::single_quote, *this))
                        {
                            ec = json_parser_errc::single_quote;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    default:
                        if (err_handler_.error(json_parser_errc::expected_name, *this))
                        {
                            ec = json_parser_errc::expected_name;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    }
                }
                break;
            case parse_state::expect_member_name: 
                {
                    switch (*p_)
                    {
                    JSONCONS_ILLEGAL_CONTROL_CHARACTER:
                        if (err_handler_.error(json_parser_errc::illegal_control_character, *this))
                        {
                            ec = json_parser_errc::illegal_control_character;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    case '\r': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::cr;
                        break; 
                    case '\n': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::lf;
                        break;   
                    case ' ':case '\t':
                        skip_whitespace();
                        break;
                    case '/': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::slash;
                        break;
                    case '\"':
                        ++p_;
                        ++column_;
                        push_state(parse_state::member_name);
                        state_ = parse_state::string_u1;
                        break;
                    case '}':
                        if (err_handler_.error(json_parser_errc::extra_comma, *this))
                        {
                            ec = json_parser_errc::extra_comma;
                            return;
                        }
                        do_end_object(ec);  // Recover
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case '\'':
                        if (err_handler_.error(json_parser_errc::single_quote, *this))
                        {
                            ec = json_parser_errc::single_quote;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    default:
                        if (err_handler_.error(json_parser_errc::expected_name, *this))
                        {
                            ec = json_parser_errc::expected_name;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    }
                }
                break;
            case parse_state::expect_colon: 
                {
                    switch (*p_)
                    {
                    JSONCONS_ILLEGAL_CONTROL_CHARACTER:
                        if (err_handler_.error(json_parser_errc::illegal_control_character, *this))
                        {
                            ec = json_parser_errc::illegal_control_character;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    case '\r': 
                        push_state(state_);
                        state_ = parse_state::cr;
                        ++p_;
                        ++column_;
                        break; 
                    case '\n': 
                        push_state(state_);
                        state_ = parse_state::lf;
                        ++p_;
                        ++column_;
                        break;   
                    case ' ':case '\t':
                        skip_whitespace();
                        break;
                    case '/': 
                        push_state(state_);
                        state_ = parse_state::slash;
                        ++p_;
                        ++column_;
                        break;
                    case ':':
                        state_ = parse_state::expect_value;
                        ++p_;
                        ++column_;
                        break;
                    default:
                        if (err_handler_.error(json_parser_errc::expected_colon, *this))
                        {
                            ec = json_parser_errc::expected_colon;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    }
                }
                break;
            case parse_state::expect_value: 
                {
                    switch (*p_)
                    {
                    JSONCONS_ILLEGAL_CONTROL_CHARACTER:
                        if (err_handler_.error(json_parser_errc::illegal_control_character, *this))
                        {
                            ec = json_parser_errc::illegal_control_character;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    case '\r': 
                        push_state(state_);
                        ++p_;
                        ++column_;
                        state_ = parse_state::cr;
                        break; 
                    case '\n': 
                        push_state(state_);
                        ++p_;
                        ++column_;
                        state_ = parse_state::lf;
                        break;   
                    case ' ':case '\t':
                        skip_whitespace();
                        break;
                    case '/': 
                        push_state(state_);
                        ++p_;
                        ++column_;
                        state_ = parse_state::slash;
                        break;
                    case '{':
                        do_begin_object(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case '[':
                        do_begin_array(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case '\"':
                        ++p_;
                        ++column_;
                        state_ = parse_state::string_u1;
                        break;
                    case '-':
                        is_negative_ = true;
                        ++p_;
                        ++column_;
                        state_ = parse_state::minus;
                        break;
                    case '0': 
                        string_buffer_.push_back(*p_);
                        ++p_;
                        ++column_;
                        state_ = parse_state::zero;
                        break;
                    case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
                        string_buffer_.push_back(*p_);
                        ++p_;
                        ++column_;
                        state_ = parse_state::integer;
                        break;
                    case 'n':
                        parse_null(ec);
                        if (ec) {return;}
                        break;
                    case 't':
                        parse_true(ec);
                        if (ec) {return;}
                        break;
                    case 'f':
                        parse_false(ec);
                        if (ec) {return;}
                        break;
                    case ']':
                        if (parent() == parse_state::array)
                        {
                            if (err_handler_.error(json_parser_errc::extra_comma, *this))
                            {
                                ec = json_parser_errc::extra_comma;
                                return;
                            }
                            do_end_array(ec);  // Recover
                            if (ec) return;
                        }
                        else
                        {
                            if (err_handler_.error(json_parser_errc::expected_value, *this))
                            {
                                ec = json_parser_errc::expected_value;
                                return;
                            }
                        }
                        ++p_;
                        ++column_;
                        break;
                    case '\'':
                        if (err_handler_.error(json_parser_errc::single_quote, *this))
                        {
                            ec = json_parser_errc::single_quote;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    default:
                        if (err_handler_.error(json_parser_errc::expected_value, *this))
                        {
                            ec = json_parser_errc::expected_value;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    }
                }
                break;
            case parse_state::expect_value_or_end: 
                {
                    switch (*p_)
                    {
                    JSONCONS_ILLEGAL_CONTROL_CHARACTER:
                        if (err_handler_.error(json_parser_errc::illegal_control_character, *this))
                        {
                            ec = json_parser_errc::illegal_control_character;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    case '\r': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::cr;
                        break; 
                    case '\n': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::lf;
                        break;   
                    case ' ':case '\t':
                        skip_whitespace();
                        break;
                    case '/': 
                        ++p_;
                        ++column_;
                        push_state(state_);
                        state_ = parse_state::slash;
                        break;
                    case '{':
                        do_begin_object(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case '[':
                        do_begin_array(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case ']':
                        do_end_array(ec);
                        if (ec) return;
                        ++p_;
                        ++column_;
                        break;
                    case '\"':
                        ++p_;
                        ++column_;
                        state_ = parse_state::string_u1;
                        break;
                    case '-':
                        is_negative_ = true;
                        ++p_;
                        ++column_;
                        state_ = parse_state::minus;
                        break;
                    case '0': 
                        string_buffer_.push_back(*p_);
                        ++p_;
                        ++column_;
                        state_ = parse_state::zero;
                        break;
                    case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
                        string_buffer_.push_back(*p_);
                        ++p_;
                        ++column_;
                        state_ = parse_state::integer;
                        break;
                    case 'n':
                        parse_null(ec);
                        if (ec) {return;}
                        break;
                    case 't':
                        parse_true(ec);
                        if (ec) {return;}
                        break;
                    case 'f':
                        parse_false(ec);
                        if (ec) {return;}
                        break;
                    case '\'':
                        if (err_handler_.error(json_parser_errc::single_quote, *this))
                        {
                            ec = json_parser_errc::single_quote;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    default:
                        if (err_handler_.error(json_parser_errc::expected_value, *this))
                        {
                            ec = json_parser_errc::expected_value;
                            return;
                        }
                        ++p_;
                        ++column_;
                        break;
                    }
                }
                break;
            case parse_state::string_u1: 
            case parse_state::escape: 
            case parse_state::escape_u1: 
            case parse_state::escape_u2: 
            case parse_state::escape_u3: 
            case parse_state::escape_u4: 
            case parse_state::escape_expect_surrogate_pair1: 
            case parse_state::escape_expect_surrogate_pair2: 
            case parse_state::escape_u6: 
            case parse_state::escape_u7: 
            case parse_state::escape_u8: 
            case parse_state::escape_u9: 
                parse_string(ec);
                if (ec) return;
                break;
            case parse_state::minus:
            case parse_state::zero:  
            case parse_state::integer: 
            case parse_state::fraction1: 
            case parse_state::fraction2: 
            case parse_state::exp1: 
            case parse_state::exp2:  
            case parse_state::exp3: 
                parse_number(ec);  
                if (ec) return;
                break;
            case parse_state::t: 
                switch (*p_)
                {
                case 'r':
                    ++p_;
                    ++column_;
                    state_ = parse_state::tr;
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                break;
            case parse_state::tr: 
                switch (*p_)
                {
                case 'u':
                    state_ = parse_state::tru;
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                ++p_;
                ++column_;
                break;
            case parse_state::tru: 
                switch (*p_)
                {
                case 'e':
                    handler_.bool_value(true,*this);
                    if (parent() == parse_state::root)
                    {
                        state_ = parse_state::done;
                        handler_.end_json();
                    }
                    else
                    {
                        state_ = parse_state::expect_comma_or_end;
                    }
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                ++p_;
                ++column_;
                break;
            case parse_state::f: 
                switch (*p_)
                {
                case 'a':
                    ++p_;
                    ++column_;
                    state_ = parse_state::fa;
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                break;
            case parse_state::fa: 
                switch (*p_)
                {
                case 'l':
                    state_ = parse_state::fal;
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                ++p_;
                ++column_;
                break;
            case parse_state::fal: 
                switch (*p_)
                {
                case 's':
                    state_ = parse_state::fals;
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                ++p_;
                ++column_;
                break;
            case parse_state::fals: 
                switch (*p_)
                {
                case 'e':
                    handler_.bool_value(false,*this);
                    if (parent() == parse_state::root)
                    {
                        state_ = parse_state::done;
                        handler_.end_json();
                    }
                    else
                    {
                        state_ = parse_state::expect_comma_or_end;
                    }
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                ++p_;
                ++column_;
                break;
            case parse_state::n: 
                switch (*p_)
                {
                case 'u':
                    ++p_;
                    ++column_;
                    state_ = parse_state::nu;
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                break;
            case parse_state::nu: 
                switch (*p_)
                {
                case 'l':
                    state_ = parse_state::nul;
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                ++p_;
                ++column_;
                break;
            case parse_state::nul: 
                switch (*p_)
                {
                case 'l':
                    handler_.null_value(*this);
                    if (parent() == parse_state::root)
                    {
                        state_ = parse_state::done;
                        handler_.end_json();
                    }
                    else
                    {
                        state_ = parse_state::expect_comma_or_end;
                    }
                    break;
                default:
                    err_handler_.error(json_parser_errc::invalid_value, *this);
                    ec = json_parser_errc::invalid_value;
                    return;
                }
                ++p_;
                ++column_;
                break;
            case parse_state::slash: 
                {
                    switch (*p_)
                    {
                    case '*':
                        state_ = parse_state::slash_star;
                        if (err_handler_.error(json_parser_errc::illegal_comment, *this))
                        {
                            ec = json_parser_errc::illegal_comment;
                            return;
                        }
                        break;
                    case '/':
                        state_ = parse_state::slash_slash;
                        if (err_handler_.error(json_parser_errc::illegal_comment, *this))
                        {
                            ec = json_parser_errc::illegal_comment;
                            return;
                        }
                        break;
                    default:    
                        if (err_handler_.error(json_parser_errc::invalid_json_text, *this))
                        {
                            ec = json_parser_errc::invalid_json_text;
                            return;
                        }
                        break;
                    }
                }
                ++p_;
                ++column_;
                break;
            case parse_state::slash_star:  
                {
                    switch (*p_)
                    {
                    case '\r':
                        push_state(state_);
                        state_ = parse_state::cr;
                        break;
                    case '\n':
                        push_state(state_);
                        state_ = parse_state::lf;
                        break;
                    case '*':
                        state_ = parse_state::slash_star_star;
                        break;
                    }
                }
                ++p_;
                ++column_;
                break;
            case parse_state::slash_slash: 
                {
                    switch (*p_)
                    {
                    case '\r':
                        state_ = pop_state();
                        break;
                    case '\n':
                        state_ = pop_state();
                        break;
                    default:
                        ++p_;
                        ++column_;
                    }
                }
                break;
            case parse_state::slash_star_star: 
                {
                    switch (*p_)
                    {
                    case '/':
                        state_ = pop_state();
                        break;
                    default:    
                        state_ = parse_state::slash_star;
                        break;
                    }
                }
                ++p_;
                ++column_;
                break;
            default:
                JSONCONS_ASSERT(false);
                break;
            }
        }
    }

    void parse_true(std::error_code& ec)
    {
        if (JSONCONS_LIKELY(end_input_ - p_ >= 4))
        {
            if (*(p_+1) == 'r' && *(p_+2) == 'u' && *(p_+3) == 'e')
            {
                handler_.bool_value(true,*this);
                p_ += 4;
                column_ += 4;
                if (parent() == parse_state::root)
                {
                    handler_.end_json();
                    state_ = parse_state::done;
                }
                else
                {
                    state_ = parse_state::expect_comma_or_end;
                }
            }
            else
            {
                err_handler_.error(json_parser_errc::invalid_value, *this);
                ec = json_parser_errc::invalid_value;
                return;
            }
        }
        else
        {
            ++p_;
            ++column_;
            state_ = parse_state::t;
        }
    }

    void parse_null(std::error_code& ec)
    {
        if (JSONCONS_LIKELY(end_input_ - p_ >= 4))
        {
            if (*(p_+1) == 'u' && *(p_+2) == 'l' && *(p_+3) == 'l')
            {
                handler_.null_value(*this);
                p_ += 4;
                column_ += 4;
                if (parent() == parse_state::root)
                {
                    handler_.end_json();
                    state_ = parse_state::done;
                }
                else
                {
                    state_ = parse_state::expect_comma_or_end;
                }
            }
            else
            {
                err_handler_.error(json_parser_errc::invalid_value, *this);
                ec = json_parser_errc::invalid_value;
                return;
            }
        }
        else
        {
            ++p_;
            ++column_;
            state_ = parse_state::n;
        }
    }

    void parse_false(std::error_code& ec)
    {
        if (JSONCONS_LIKELY(end_input_ - p_ >= 5))
        {
            if (*(p_+1) == 'a' && *(p_+2) == 'l' && *(p_+3) == 's' && *(p_+4) == 'e')
            {
                handler_.bool_value(false,*this);
                p_ += 5;
                column_ += 5;
                if (parent() == parse_state::root)
                {
                    handler_.end_json();
                    state_ = parse_state::done;
                }
                else
                {
                    state_ = parse_state::expect_comma_or_end;
                }
            }
            else
            {
                err_handler_.error(json_parser_errc::invalid_value, *this);
                ec = json_parser_errc::invalid_value;
                return;
            }
        }
        else
        {
            ++p_;
            ++column_;
            state_ = parse_state::f;
        }
    }

    void parse_number(std::error_code& ec)
    {
        const CharT* local_end_input = end_input_;
        const CharT* sb = p_;

        switch (state_)
        {
        case parse_state::minus:
            goto minus;
        case parse_state::zero:
            goto zero;
        case parse_state::integer:
            goto integer;
        case parse_state::fraction1:
            goto fraction1;
        case parse_state::fraction2:
            goto fraction2;
        case parse_state::exp1:
            goto exp1;
        case parse_state::exp2:
            goto exp2;
        case parse_state::exp3:
            goto exp3;
        default:
            JSONCONS_UNREACHABLE();               
        }

minus:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::minus;
            return;
        }
        switch (*p_)
        {
        case '0': 
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto zero;
        case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto integer;
        default:
            err_handler_.error(json_parser_errc::expected_value, *this);
            ec = json_parser_errc::expected_value;
            state_ = parse_state::minus;
            return;
        }
zero:
            if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
            {
                state_ = parse_state::zero;
                return;
            }
            switch (*p_)
            {
            case '\r': 
                end_integer_value(ec);
                if (ec) return;
                ++p_;
                ++column_;
                push_state(state_);
                state_ = parse_state::cr;
                return; 
            case '\n': 
                end_integer_value(ec);
                if (ec) return;
                push_state(state_);
                ++p_;
                ++column_;
                state_ = parse_state::lf;
                return;   
            case ' ':case '\t':
                end_integer_value(ec);
                if (ec) return;
                skip_whitespace();
                return;
            case '/': 
                end_integer_value(ec);
                if (ec) return;
                ++p_;
                ++column_;
                push_state(state_);
                state_ = parse_state::slash;
                return;
            case '}':
                end_integer_value(ec);
                if (ec) return;
                do_end_object(ec);
                ++p_;
                ++column_;
                if (ec) return;
                return;
            case ']':
                end_integer_value(ec);
                if (ec) return;
                do_end_array(ec);
                ++p_;
                ++column_;
                if (ec) return;
                return;
            case '.':
                precision_ = static_cast<uint8_t>(string_buffer_.length());
                string_buffer_.push_back(*p_);
                ++p_;
                ++column_;
                goto fraction1;
            case 'e':case 'E':
                precision_ = static_cast<uint8_t>(string_buffer_.length());
                string_buffer_.push_back(*p_);
                ++p_;
                ++column_;
                goto exp1;
            case ',':
                end_integer_value(ec);
                if (ec) return;
                begin_member_or_element(ec);
                if (ec) return;
                ++p_;
                ++column_;
                return;
            case '0': case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
                err_handler_.error(json_parser_errc::leading_zero, *this);
                ec = json_parser_errc::leading_zero;
                state_ = parse_state::zero;
                return;
            default:
                err_handler_.error(json_parser_errc::invalid_number, *this);
                ec = json_parser_errc::invalid_number;
                state_ = parse_state::zero;
                return;
            }
integer:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::integer;
            return;
        }
        switch (*p_)
        {
        case '\r': 
            end_integer_value(ec);
            if (ec) return;
            push_state(state_);
            ++p_;
            ++column_;
            state_ = parse_state::cr;
            return; 
        case '\n': 
            end_integer_value(ec);
            if (ec) return;
            push_state(state_);
            ++p_;
            ++column_;
            state_ = parse_state::lf;
            return;   
        case ' ':case '\t':
            end_integer_value(ec);
            if (ec) return;
            skip_whitespace();
            return;
        case '/': 
            end_integer_value(ec);
            if (ec) return;
            push_state(state_);
            ++p_;
            ++column_;
            state_ = parse_state::slash;
            return;
        case '}':
            end_integer_value(ec);
            if (ec) return;
            do_end_object(ec);
            if (ec) return;
            ++p_;
            ++column_;
            return;
        case ']':
            end_integer_value(ec);
            if (ec) return;
            do_end_array(ec);
            if (ec) return;
            ++p_;
            ++column_;
            return;
        case '0': 
        case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto integer;
        case '.':
            precision_ = static_cast<uint8_t>(string_buffer_.length());
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto fraction1;
        case ',':
            end_integer_value(ec);
            if (ec) return;
            begin_member_or_element(ec);
            if (ec) return;
            ++p_;
            ++column_;
            return;
        case 'e':case 'E':
            precision_ = static_cast<uint8_t>(string_buffer_.length());
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto exp1;
        default:
            err_handler_.error(json_parser_errc::invalid_number, *this);
            ec = json_parser_errc::invalid_number;
            state_ = parse_state::integer;
            return;
        }
fraction1:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::fraction1;
            return;
        }
        switch (*p_)
        {
        case '0': 
        case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
            ++precision_;
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto fraction2;
        default:
            err_handler_.error(json_parser_errc::invalid_number, *this);
            ec = json_parser_errc::invalid_number;
            state_ = parse_state::fraction1;
            return;
        }
fraction2:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::fraction2;
            return;
        }
        switch (*p_)
        {
        case '\r': 
            end_fraction_value(ec);
            if (ec) return;
            push_state(state_);
            ++p_;
            ++column_;
            state_ = parse_state::cr;
            return; 
        case '\n': 
            end_fraction_value(ec);
            if (ec) return;
            push_state(state_);
            ++p_;
            ++column_;
            state_ = parse_state::lf;
            return;   
        case ' ':case '\t':
            end_fraction_value(ec);
            if (ec) return;
            skip_whitespace();
            return;
        case '/': 
            end_fraction_value(ec);
            if (ec) return;
            push_state(state_);
            ++p_;
            ++column_;
            state_ = parse_state::slash;
            return;
        case '}':
            end_fraction_value(ec);
            if (ec) return;
            do_end_object(ec);
            if (ec) return;
            ++p_;
            ++column_;
            return;
        case ']':
            end_fraction_value(ec);
            if (ec) return;
            do_end_array(ec);
            if (ec) return;
            ++p_;
            ++column_;
            return;
        case '0': 
        case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
            ++precision_;
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto fraction2;
        case ',':
            end_fraction_value(ec);
            if (ec) return;
            begin_member_or_element(ec);
            if (ec) return;
            ++p_;
            ++column_;
            return;
        case 'e':case 'E':
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto exp1;
        default:
            err_handler_.error(json_parser_errc::invalid_number, *this);
            ec = json_parser_errc::invalid_number;
            state_ = parse_state::fraction2;
            return;
        }
exp1:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::exp1;
            return;
        }
        switch (*p_)
        {
        case '+':
            ++p_;
            ++column_;
            goto exp2;
        case '-':
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto exp2;
        case '0': 
        case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto exp3;
        default:
            err_handler_.error(json_parser_errc::expected_value, *this);
            ec = json_parser_errc::expected_value;
            state_ = parse_state::exp1;
            return;
        }
exp2:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::exp2;
            return;
        }
        switch (*p_)
        {
        case '0': 
        case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto exp3;
        default:
            err_handler_.error(json_parser_errc::expected_value, *this);
            ec = json_parser_errc::expected_value;
            state_ = parse_state::exp2;
            return;
        }
        
exp3:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::exp3;
            return;
        }
        switch (*p_)
        {
        case '\r': 
            end_fraction_value(ec);
            if (ec) return;
            ++p_;
            ++column_;
            push_state(state_);
            state_ = parse_state::cr;
            return; 
        case '\n': 
            end_fraction_value(ec);
            if (ec) return;
            ++p_;
            ++column_;
            push_state(state_);
            state_ = parse_state::lf;
            return;   
        case ' ':case '\t':
            end_fraction_value(ec);
            if (ec) return;
            skip_whitespace();
            return;
        case '/': 
            end_fraction_value(ec);
            if (ec) return;
            push_state(state_);
            ++p_;
            ++column_;
            state_ = parse_state::slash;
            return;
        case '}':
            end_fraction_value(ec);
            if (ec) return;
            do_end_object(ec);
            if (ec) return;
            ++p_;
            ++column_;
            return;
        case ']':
            end_fraction_value(ec);
            if (ec) return;
            do_end_array(ec);
            if (ec) return;
            ++p_;
            ++column_;
            return;
        case ',':
            end_fraction_value(ec);
            if (ec) return;
            begin_member_or_element(ec);
            if (ec) return;
            ++p_;
            ++column_;
            return;
        case '0': 
        case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
            string_buffer_.push_back(*p_);
            ++p_;
            ++column_;
            goto exp3;
        default:
            err_handler_.error(json_parser_errc::invalid_number, *this);
            ec = json_parser_errc::invalid_number;
            state_ = parse_state::exp3;
            return;
        }
        JSONCONS_UNREACHABLE();               
    }

    void parse_string(std::error_code& ec)
    {
        const CharT* local_end_input = end_input_;
        const CharT* sb = p_;

        switch (state_)
        {
        case parse_state::string_u1:
            goto string_u1;
        case parse_state::escape:
            goto escape;
        case parse_state::escape_u1:
            goto escape_u1;
        case parse_state::escape_u2:
            goto escape_u2;
        case parse_state::escape_u3:
            goto escape_u3;
        case parse_state::escape_u4:
            goto escape_u4;
        case parse_state::escape_expect_surrogate_pair1:
            goto escape_expect_surrogate_pair1;
        case parse_state::escape_expect_surrogate_pair2:
            goto escape_expect_surrogate_pair2;
        case parse_state::escape_u6:
            goto escape_u6;
        case parse_state::escape_u7:
            goto escape_u7;
        case parse_state::escape_u8:
            goto escape_u8;
        case parse_state::escape_u9:
            goto escape_u9;
        default:
            JSONCONS_UNREACHABLE();               
        }

string_u1:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            auto result = unicons::validate(sb,p_);
            if (result.ec != unicons::conv_errc())
            {
                translate_conv_errc(result.ec,ec);
                column_ += (result.it - sb);
                return;
            }
            string_buffer_.append(sb,p_-sb);
            column_ += (p_ - sb + 1);
            state_ = parse_state::string_u1;
            return;
        }
        switch (*p_)
        {
            JSONCONS_ILLEGAL_CONTROL_CHARACTER:
            {
                column_ += (p_ - sb + 1);
                if (err_handler_.error(json_parser_errc::illegal_control_character, *this))
                {
                    ec = json_parser_errc::illegal_control_character;
                    state_ = parse_state::string_u1;
                    return;
                }
                // recovery - skip
                auto result = unicons::validate(sb,p_);
                if (result.ec != unicons::conv_errc())
                {
                    translate_conv_errc(result.ec,ec);
                    column_ += (result.it - sb);
                    return;
                }
                string_buffer_.append(sb,p_-sb);
                ++p_;
                state_ = parse_state::string_u1;
                return;
            }
            case '\r':
            {
                column_ += (p_ - sb + 1);
                if (err_handler_.error(json_parser_errc::illegal_character_in_string, *this))
                {
                    ec = json_parser_errc::illegal_character_in_string;
                    state_ = parse_state::string_u1;
                    return;
                }
                // recovery - keep
                auto result = unicons::validate(sb,p_);
                if (result.ec != unicons::conv_errc())
                {
                    translate_conv_errc(result.ec,ec);
                    column_ += (result.it - sb);
                    return;
                }
                string_buffer_.append(sb, p_ - sb + 1);
                ++p_;
                push_state(state_);
                state_ = parse_state::cr;
                return;
            }
            case '\n':
            {
                column_ += (p_ - sb + 1);
                if (err_handler_.error(json_parser_errc::illegal_character_in_string, *this))
                {
                    ec = json_parser_errc::illegal_character_in_string;
                    state_ = parse_state::string_u1;
                    return;
                }
                // recovery - keep
                auto result = unicons::validate(sb,p_);
                if (result.ec != unicons::conv_errc())
                {
                    translate_conv_errc(result.ec,ec);
                    column_ += (result.it - sb);
                    return;
                }
                string_buffer_.append(sb, p_ - sb + 1);
                ++p_;
                push_state(state_);
                state_ = parse_state::lf;
                return;
            }
            case '\t':
            {
                column_ += (p_ - sb + 1);
                if (err_handler_.error(json_parser_errc::illegal_character_in_string, *this))
                {
                    ec = json_parser_errc::illegal_character_in_string;
                    state_ = parse_state::string_u1;
                    return;
                }
                // recovery - keep
                auto result = unicons::validate(sb,p_);
                if (result.ec != unicons::conv_errc())
                {
                    translate_conv_errc(result.ec,ec);
                    column_ += (result.it - sb);
                    return;
                }
                string_buffer_.append(sb, p_ - sb + 1);
                ++p_;
                state_ = parse_state::string_u1;
                return;
            }
            case '\\': 
            {
                auto result = unicons::validate(sb,p_);
                if (result.ec != unicons::conv_errc())
                {
                    translate_conv_errc(result.ec,ec);
                    column_ += (result.it - sb);
                    return;
                }
                string_buffer_.append(sb,p_-sb);
                column_ += (p_ - sb + 1);
                ++p_;
                goto escape;
            }
            case '\"':
            {
                auto result = unicons::validate(sb,p_);
                if (result.ec != unicons::conv_errc())
                {
                    translate_conv_errc(result.ec,ec);
                    column_ += (result.it - sb);
                    return;
                }
                if (string_buffer_.length() == 0)
                {
                    end_string_value(sb,p_-sb, ec);
                    if (ec) {return;}
                }
                else
                {
                    string_buffer_.append(sb,p_-sb);
                    end_string_value(string_buffer_.data(),string_buffer_.length(), ec);
                    string_buffer_.clear();
                    if (ec) {return;}
                }
                column_ += (p_ - sb + 1);
                ++p_;
                return;
            }
            default:
                ++p_;
                goto string_u1;
            }

escape:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape;
            return;
        }
        switch (*p_)
        {
        case '\"':
            string_buffer_.push_back('\"');
            sb = ++p_;
            ++column_;
            goto string_u1;
        case '\\': 
            string_buffer_.push_back('\\');
            sb = ++p_;
            ++column_;
            goto string_u1;
        case '/':
            string_buffer_.push_back('/');
            sb = ++p_;
            ++column_;
            goto string_u1;
        case 'b':
            string_buffer_.push_back('\b');
            sb = ++p_;
            ++column_;
            goto string_u1;
        case 'f':
            string_buffer_.push_back('\f');
            sb = ++p_;
            ++column_;
            goto string_u1;
        case 'n':
            string_buffer_.push_back('\n');
            sb = ++p_;
            ++column_;
            goto string_u1;
        case 'r':
            string_buffer_.push_back('\r');
            sb = ++p_;
            ++column_;
            goto string_u1;
        case 't':
            string_buffer_.push_back('\t');
            sb = ++p_;
            ++column_;
            goto string_u1;
        case 'u':
            cp_ = 0;
            ++p_;
            ++column_;
            goto escape_u1;
        default:    
            err_handler_.error(json_parser_errc::illegal_escaped_character, *this);
            ec = json_parser_errc::illegal_escaped_character;
            state_ = parse_state::escape;
            return;
        }

escape_u1:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_u1;
            return;
        }
        {
            append_codepoint(*p_,ec);
            if (ec)
            {
                state_ = parse_state::escape_u1;
                return;
            }
            ++p_;
            ++column_;
            goto escape_u2;
        }

escape_u2:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_u2;
            return;
        }
        {
            append_codepoint(*p_, ec);
            if (ec)
            {
                state_ = parse_state::escape_u2;
                return;
            }
            ++p_;
            ++column_;
            goto escape_u3;
        }

escape_u3:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_u3;
            return;
        }
        {
            append_codepoint(*p_, ec);
            if (ec)
            {
                state_ = parse_state::escape_u3;
                return;
            }
            ++p_;
            ++column_;
            goto escape_u4;
        }

escape_u4:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_u4;
            return;
        }
        {
            append_codepoint(*p_, ec);
            if (ec)
            {
                state_ = parse_state::escape_u4;
                return;
            }
            if (unicons::is_high_surrogate(cp_))
            {
                ++p_;
                ++column_;
                goto escape_expect_surrogate_pair1;
            }
            else
            {
                unicons::convert(&cp_, &cp_ + 1, std::back_inserter(string_buffer_));
                sb = ++p_;
                ++column_;
                state_ = parse_state::string_u1;
                return;
            }
        }

escape_expect_surrogate_pair1:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_expect_surrogate_pair1;
            return;
        }
        {
            switch (*p_)
            {
            case '\\': 
                cp2_ = 0;
                ++p_;
                ++column_;
                goto escape_expect_surrogate_pair2;
            default:
                err_handler_.error(json_parser_errc::expected_codepoint_surrogate_pair, *this);
                ec = json_parser_errc::expected_codepoint_surrogate_pair;
                state_ = parse_state::escape_expect_surrogate_pair1;
                return;
            }
        }

escape_expect_surrogate_pair2:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_expect_surrogate_pair2;
            return;
        }
        {
            switch (*p_)
            {
            case 'u':
                ++p_;
                ++column_;
                goto escape_u6;
            default:
                err_handler_.error(json_parser_errc::expected_codepoint_surrogate_pair, *this);
                ec = json_parser_errc::expected_codepoint_surrogate_pair;
                state_ = parse_state::escape_expect_surrogate_pair2;
                return;
            }
        }

escape_u6:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_u6;
            return;
        }
        {
            append_second_codepoint(*p_, ec);
            if (ec)
            {
                state_ = parse_state::escape_u6;
                return;
            }
        }
        ++p_;
        ++column_;
        goto escape_u7;

escape_u7:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_u7;
            return;
        }
        {
            append_second_codepoint(*p_, ec);
            if (ec)
            {
                state_ = parse_state::escape_u7;
                return;
            }
            ++p_;
            ++column_;
            goto escape_u8;
        }

escape_u8:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_u8;
            return;
        }
        {
            append_second_codepoint(*p_, ec);
            if (ec)
            {
                state_ = parse_state::escape_u8;
                return;
            }
            ++p_;
            ++column_;
            goto escape_u9;
        }

escape_u9:
        if (JSONCONS_UNLIKELY(p_ >= local_end_input)) // Buffer exhausted               
        {
            state_ = parse_state::escape_u9;
            return;
        }
        {
            append_second_codepoint(*p_, ec);
            if (ec)
            {
                state_ = parse_state::escape_u9;
                return;
            }
            uint32_t cp = 0x10000 + ((cp_ & 0x3FF) << 10) + (cp2_ & 0x3FF);
            unicons::convert(&cp, &cp + 1, std::back_inserter(string_buffer_));
            sb = ++p_;
            ++column_;
            goto string_u1;
        }

        JSONCONS_UNREACHABLE();               
    }

    void translate_conv_errc(unicons::conv_errc result, std::error_code& ec)
    {
        switch (result)
        {
        case unicons::conv_errc():
            break;
        case unicons::conv_errc::over_long_utf8_sequence:
            if (err_handler_.error(json_parser_errc::over_long_utf8_sequence, *this))
            {
                ec = json_parser_errc::over_long_utf8_sequence;
                return;
            }
            break;
        case unicons::conv_errc::unpaired_high_surrogate:
            if (err_handler_.error(json_parser_errc::unpaired_high_surrogate, *this))
            {
                ec = json_parser_errc::unpaired_high_surrogate;
                return;
            }
            break;
        case unicons::conv_errc::expected_continuation_byte:
            if (err_handler_.error(json_parser_errc::expected_continuation_byte, *this))
            {
                ec = json_parser_errc::expected_continuation_byte;
                return;
            }
            break;
        case unicons::conv_errc::illegal_surrogate_value:
            if (err_handler_.error(json_parser_errc::illegal_surrogate_value, *this))
            {
                ec = json_parser_errc::illegal_surrogate_value;
                return;
            }
            break;
        default:
            if (err_handler_.error(json_parser_errc::illegal_codepoint, *this))
            {
                ec = json_parser_errc::illegal_codepoint;
                return;
            }
            break;
        }
    }

    void parse()
    {
        std::error_code ec;
        parse(ec);
        if (ec)
        {
            throw parse_error(ec,line_,column_);
        }
    }

    void end_parse()
    {
        std::error_code ec;
        end_parse(ec);
        if (ec)
        {
            throw parse_error(ec,line_,column_);
        }
    }

    void end_parse(std::error_code& ec)
    {
        if (parent() == parse_state::root)
        {
            switch (state_)
            {
            case parse_state::zero:  
            case parse_state::integer:
                end_integer_value(ec);
                if (ec) return;
                break;
            case parse_state::fraction2:
            case parse_state::exp3:
                end_fraction_value(ec);
                if (ec) return;
                break;
            default:
                break;
            }
        }
        if (state_ == parse_state::lf || state_ == parse_state::cr)
        { 
            state_ = pop_state();
        }
        if (!(state_ == parse_state::done || state_ == parse_state::start))
        {
            if (err_handler_.error(json_parser_errc::unexpected_eof, *this))
            {
                ec = json_parser_errc::unexpected_eof;
                return;
            }
        }
    }

    parse_state state() const
    {
        return state_;
    }

    void set_source(const CharT* input, size_t length)
    {
        begin_input_ = input;
        end_input_ = input + length;
        p_ = begin_input_;
    }
private:
    void end_fraction_value(std::error_code& ec)
    {
        try
        {
            double d = str_to_double_(string_buffer_.data(), precision_);
            if (is_negative_)
                d = -d;
            handler_.double_value(d, static_cast<uint8_t>(precision_), *this);
        }
        catch (...)
        {
            if (err_handler_.error(json_parser_errc::invalid_number, *this))
            {
                ec = json_parser_errc::invalid_number;
                return;
            }
            handler_.null_value(*this); // recovery
        }
        string_buffer_.clear();
        is_negative_ = false;

        switch (parent())
        {
        case parse_state::array:
        case parse_state::object:
            state_ = parse_state::expect_comma_or_end;
            break;
        case parse_state::root:
            state_ = parse_state::done;
            handler_.end_json();
            break;
        default:
            if (err_handler_.error(json_parser_errc::invalid_json_text, *this))
            {
                ec = json_parser_errc::invalid_json_text;
                return;
            }
            break;
        }
    }

    void end_integer_value(std::error_code& ec)
    {
        if (is_negative_)
        {
            int64_t d;
            if (try_string_to_integer(is_negative_, string_buffer_.data(), string_buffer_.length(),d))
            {
                handler_.integer_value(d, *this);
            }
            else
            {
                try
                {
                    double d2 = str_to_double_(string_buffer_.data(), string_buffer_.length());
                    handler_.double_value(-d2, static_cast<uint8_t>(string_buffer_.length()), *this);
                }
                catch (...)
                {
                    if (err_handler_.error(json_parser_errc::invalid_number, *this))
                    {
                        ec = json_parser_errc::invalid_number;
                        return;
                    }
                    handler_.null_value(*this);
                }
            }
        }
        else
        {
            uint64_t d;
            if (try_string_to_uinteger(string_buffer_.data(), string_buffer_.length(),d))
            {
                handler_.uinteger_value(d, *this);
            }
            else
            {
                try
                {
                    double d2 = str_to_double_(string_buffer_.data(),string_buffer_.length());
                    handler_.double_value(d2, static_cast<uint8_t>(string_buffer_.length()), *this);
                }
                catch (...)
                {
                    if (err_handler_.error(json_parser_errc::invalid_number, *this))
                    {
                        ec = json_parser_errc::invalid_number;
                        return;
                    }
                    handler_.null_value(*this);
                }
            }
        }

        switch (parent())
        {
        case parse_state::array:
        case parse_state::object:
            state_ = parse_state::expect_comma_or_end;
            break;
        case parse_state::root:
            state_ = parse_state::done;
            handler_.end_json();
            break;
        default:
            if (err_handler_.error(json_parser_errc::invalid_json_text, *this))
            {
                ec = json_parser_errc::invalid_json_text;
                return;
            }
            break;
        }
        string_buffer_.clear();
        is_negative_ = false;
    }

    void append_codepoint(int c, std::error_code& ec)
    {
        switch (c)
        {
        case '0': case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
        case 'a':case 'b':case 'c':case 'd':case 'e':case 'f':
        case 'A':case 'B':case 'C':case 'D':case 'E':case 'F':
            cp_ = append_to_codepoint(cp_, c, ec);
            if (ec) return;
            break;
        default:
            if (err_handler_.error(json_parser_errc::expected_value, *this))
            {
                ec = json_parser_errc::expected_value;
                return;
            }
            break;
        }
    }

    void append_second_codepoint(int c, std::error_code& ec)
    {
        switch (c)
        {
        case '0': 
        case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8': case '9':
        case 'a':case 'b':case 'c':case 'd':case 'e':case 'f':
        case 'A':case 'B':case 'C':case 'D':case 'E':case 'F':
            cp2_ = append_to_codepoint(cp2_, c, ec);
            if (ec) return;
            break;
        default:
            if (err_handler_.error(json_parser_errc::expected_value, *this))
            {
                ec = json_parser_errc::expected_value;
                return;
            }
            break;
        }
    }

    void end_string_value(const CharT* s, size_t length, std::error_code& ec) 
    {
        switch (parent())
        {
        case parse_state::member_name:
            handler_.name(string_view_type(s, length), *this);
            state_ = pop_state();
            state_ = parse_state::expect_colon;
            break;
        case parse_state::object:
        case parse_state::array:
            handler_.string_value(string_view_type(s, length), *this);
            state_ = parse_state::expect_comma_or_end;
            break;
        case parse_state::root:
            handler_.string_value(string_view_type(s, length), *this);
            state_ = parse_state::done;
            handler_.end_json();
            break;
        default:
            if (err_handler_.error(json_parser_errc::invalid_json_text, *this))
            {
                ec = json_parser_errc::invalid_json_text;
                return;
            }
            break;
        }
    }

    void begin_member_or_element(std::error_code& ec) 
    {
        switch (parent())
        {
        case parse_state::object:
            state_ = parse_state::expect_member_name;
            break;
        case parse_state::array:
            state_ = parse_state::expect_value;
            break;
        case parse_state::root:
            break;
        default:
            if (err_handler_.error(json_parser_errc::invalid_json_text, *this))
            {
                ec = json_parser_errc::invalid_json_text;
                return;
            }
            break;
        }
    }

    void push_state(parse_state state)
    {
        state_stack_.push_back(state);
    }

    parse_state pop_state()
    {
        JSONCONS_ASSERT(!state_stack_.empty())
        parse_state state = state_stack_.back();
        state_stack_.pop_back();
        return state;
    }
 
    uint32_t append_to_codepoint(uint32_t cp, int c, std::error_code& ec)
    {
        cp *= 16;
        if (c >= '0'  &&  c <= '9')
        {
            cp += c - '0';
        }
        else if (c >= 'a'  &&  c <= 'f')
        {
            cp += c - 'a' + 10;
        }
        else if (c >= 'A'  &&  c <= 'F')
        {
            cp += c - 'A' + 10;
        }
        else
        {
            if (err_handler_.error(json_parser_errc::invalid_hex_escape_sequence, *this))
            {
                ec = json_parser_errc::invalid_hex_escape_sequence;
                return cp;
            }
        }
        return cp;
    }

    size_t do_line_number() const override
    {
        return line_;
    }

    size_t do_column_number() const override
    {
        return column_;
    }
};

typedef basic_json_parser<char> json_parser;
typedef basic_json_parser<wchar_t> wjson_parser;

}

#endif

