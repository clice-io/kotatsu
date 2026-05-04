#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "simdjson.h"
#include "benchmark/benchmark.h"
#include "kota/codec/json/json.h"

namespace {

using kota::codec::json::Serializer;
using kota::codec::json::from_json;
using kota::codec::json::to_json;

struct fixed_object_t {
    std::vector<int> int_array;
    std::vector<float> float_array;
    std::vector<double> double_array;
};

struct fixed_name_object_t {
    std::string name0{};
    std::string name1{};
    std::string name2{};
    std::string name3{};
    std::string name4{};
};

struct nested_object_t {
    std::vector<std::array<double, 3>> v3s{};
    std::string id{};
};

struct another_object_t {
    std::string string{};
    std::string another_string{};
    std::string escaped_text{};
    bool boolean{};
    nested_object_t nested_object{};
};

struct obj_t {
    fixed_object_t fixed_object{};
    fixed_name_object_t fixed_name_object{};
    another_object_t another_object{};
    std::vector<std::string> string_array{};
    std::string string{};
    double number{};
    bool boolean{};
    bool another_bool{};
};

constexpr std::string_view json_minified =
    R"({"fixed_object":{"int_array":[0,1,2,3,4,5,6],"float_array":[0.1,0.2,0.3,0.4,0.5,0.6],"double_array":[3288398.238,2.33e+24,28.9,0.928759872,0.22222848,0.1,0.2,0.3,0.4]},"fixed_name_object":{"name0":"James","name1":"Abraham","name2":"Susan","name3":"Frank","name4":"Alicia"},"another_object":{"string":"here is some text","another_string":"Hello World","escaped_text":"{\"some key\":\"some string value\"}","boolean":false,"nested_object":{"v3s":[[0.12345,0.23456,0.001345],[0.3894675,97.39827,297.92387],[18.18,87.289,2988.298]],"id":"298728949872"}},"string_array":["Cat","Dog","Elephant","Tiger"],"string":"Hello world","number":3.14,"boolean":true,"another_bool":false})";

// -- handwritten simdjson helpers --

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4834)
#endif

void handwritten_read_nested(simdjson::ondemand::object& obj, nested_object_t& out) {
    for(auto field_result: obj) {
        simdjson::ondemand::field field;
        (void)std::move(field_result).get(field);
        std::string_view key;
        (void)field.unescaped_key().get(key);
        if(key == "v3s") {
            out.v3s.clear();
            simdjson::ondemand::array arr;
            (void)field.value().get_array().get(arr);
            for(auto inner_result: arr) {
                simdjson::ondemand::value inner_val;
                (void)std::move(inner_result).get(inner_val);
                simdjson::ondemand::array inner_arr;
                (void)inner_val.get_array().get(inner_arr);
                std::array<double, 3> v{};
                std::size_t i = 0;
                for(auto elem_result: inner_arr) {
                    simdjson::ondemand::value elem;
                    (void)std::move(elem_result).get(elem);
                    double d;
                    (void)elem.get_double().get(d);
                    if(i < 3)
                        v[i++] = d;
                }
                out.v3s.push_back(v);
            }
        } else if(key == "id") {
            std::string_view sv;
            (void)field.value().get_string().get(sv);
            out.id = sv;
        }
    }
}

void handwritten_read_another(simdjson::ondemand::object& obj, another_object_t& out) {
    for(auto field_result: obj) {
        simdjson::ondemand::field field;
        (void)std::move(field_result).get(field);
        std::string_view key;
        (void)field.unescaped_key().get(key);
        if(key == "string") {
            std::string_view sv;
            (void)field.value().get_string().get(sv);
            out.string = sv;
        } else if(key == "another_string") {
            std::string_view sv;
            (void)field.value().get_string().get(sv);
            out.another_string = sv;
        } else if(key == "escaped_text") {
            std::string_view sv;
            (void)field.value().get_string().get(sv);
            out.escaped_text = sv;
        } else if(key == "boolean") {
            bool b;
            (void)field.value().get_bool().get(b);
            out.boolean = b;
        } else if(key == "nested_object") {
            simdjson::ondemand::object nested;
            (void)field.value().get_object().get(nested);
            handwritten_read_nested(nested, out.nested_object);
        }
    }
}

void handwritten_read_obj(simdjson::ondemand::document& doc, obj_t& out) {
    simdjson::ondemand::object root;
    (void)doc.get_object().get(root);
    for(auto field_result: root) {
        simdjson::ondemand::field field;
        (void)std::move(field_result).get(field);
        std::string_view key;
        (void)field.unescaped_key().get(key);
        if(key == "fixed_object") {
            simdjson::ondemand::object obj;
            (void)field.value().get_object().get(obj);
            for(auto f2_result: obj) {
                simdjson::ondemand::field f2;
                (void)std::move(f2_result).get(f2);
                std::string_view k2;
                (void)f2.unescaped_key().get(k2);
                if(k2 == "int_array") {
                    out.fixed_object.int_array.clear();
                    simdjson::ondemand::array arr;
                    (void)f2.value().get_array().get(arr);
                    for(auto e_result: arr) {
                        simdjson::ondemand::value e;
                        (void)std::move(e_result).get(e);
                        std::int64_t v;
                        (void)e.get_int64().get(v);
                        out.fixed_object.int_array.push_back(static_cast<int>(v));
                    }
                } else if(k2 == "float_array") {
                    out.fixed_object.float_array.clear();
                    simdjson::ondemand::array arr;
                    (void)f2.value().get_array().get(arr);
                    for(auto e_result: arr) {
                        simdjson::ondemand::value e;
                        (void)std::move(e_result).get(e);
                        double d;
                        (void)e.get_double().get(d);
                        out.fixed_object.float_array.push_back(static_cast<float>(d));
                    }
                } else if(k2 == "double_array") {
                    out.fixed_object.double_array.clear();
                    simdjson::ondemand::array arr;
                    (void)f2.value().get_array().get(arr);
                    for(auto e_result: arr) {
                        simdjson::ondemand::value e;
                        (void)std::move(e_result).get(e);
                        double d;
                        (void)e.get_double().get(d);
                        out.fixed_object.double_array.push_back(d);
                    }
                }
            }
        } else if(key == "fixed_name_object") {
            simdjson::ondemand::object obj;
            (void)field.value().get_object().get(obj);
            for(auto f2_result: obj) {
                simdjson::ondemand::field f2;
                (void)std::move(f2_result).get(f2);
                std::string_view k2;
                (void)f2.unescaped_key().get(k2);
                std::string_view sv;
                (void)f2.value().get_string().get(sv);
                if(k2 == "name0")
                    out.fixed_name_object.name0 = sv;
                else if(k2 == "name1")
                    out.fixed_name_object.name1 = sv;
                else if(k2 == "name2")
                    out.fixed_name_object.name2 = sv;
                else if(k2 == "name3")
                    out.fixed_name_object.name3 = sv;
                else if(k2 == "name4")
                    out.fixed_name_object.name4 = sv;
            }
        } else if(key == "another_object") {
            simdjson::ondemand::object obj;
            (void)field.value().get_object().get(obj);
            handwritten_read_another(obj, out.another_object);
        } else if(key == "string_array") {
            out.string_array.clear();
            simdjson::ondemand::array arr;
            (void)field.value().get_array().get(arr);
            for(auto e_result: arr) {
                simdjson::ondemand::value e;
                (void)std::move(e_result).get(e);
                std::string_view sv;
                (void)e.get_string().get(sv);
                out.string_array.push_back(std::string(sv));
            }
        } else if(key == "string") {
            std::string_view sv;
            (void)field.value().get_string().get(sv);
            out.string = sv;
        } else if(key == "number") {
            (void)field.value().get_double().get(out.number);
        } else if(key == "boolean") {
            (void)field.value().get_bool().get(out.boolean);
        } else if(key == "another_bool") {
            (void)field.value().get_bool().get(out.another_bool);
        }
    }
}

void sb_field(simdjson::builder::string_builder& sb, std::string_view name, bool first = false) {
    if(!first)
        sb.append_comma();
    sb.escape_and_append_with_quotes(name);
    sb.append_colon();
}

void sb_next(simdjson::builder::string_builder& sb, bool first = false) {
    if(!first)
        sb.append_comma();
}

void handwritten_write_obj(simdjson::builder::string_builder& sb, const obj_t& obj) {
    sb.start_object();

    sb_field(sb, "fixed_object", true);
    sb.start_object();
    sb_field(sb, "int_array", true);
    sb.start_array();
    for(std::size_t i = 0; i < obj.fixed_object.int_array.size(); ++i) {
        sb_next(sb, i == 0);
        sb.append(static_cast<std::int64_t>(obj.fixed_object.int_array[i]));
    }
    sb.end_array();
    sb_field(sb, "float_array");
    sb.start_array();
    for(std::size_t i = 0; i < obj.fixed_object.float_array.size(); ++i) {
        sb_next(sb, i == 0);
        sb.append(static_cast<double>(obj.fixed_object.float_array[i]));
    }
    sb.end_array();
    sb_field(sb, "double_array");
    sb.start_array();
    for(std::size_t i = 0; i < obj.fixed_object.double_array.size(); ++i) {
        sb_next(sb, i == 0);
        sb.append(obj.fixed_object.double_array[i]);
    }
    sb.end_array();
    sb.end_object();

    sb_field(sb, "fixed_name_object");
    sb.start_object();
    sb_field(sb, "name0", true);
    sb.escape_and_append_with_quotes(obj.fixed_name_object.name0);
    sb_field(sb, "name1");
    sb.escape_and_append_with_quotes(obj.fixed_name_object.name1);
    sb_field(sb, "name2");
    sb.escape_and_append_with_quotes(obj.fixed_name_object.name2);
    sb_field(sb, "name3");
    sb.escape_and_append_with_quotes(obj.fixed_name_object.name3);
    sb_field(sb, "name4");
    sb.escape_and_append_with_quotes(obj.fixed_name_object.name4);
    sb.end_object();

    sb_field(sb, "another_object");
    sb.start_object();
    sb_field(sb, "string", true);
    sb.escape_and_append_with_quotes(obj.another_object.string);
    sb_field(sb, "another_string");
    sb.escape_and_append_with_quotes(obj.another_object.another_string);
    sb_field(sb, "escaped_text");
    sb.escape_and_append_with_quotes(obj.another_object.escaped_text);
    sb_field(sb, "boolean");
    sb.append(obj.another_object.boolean);
    sb_field(sb, "nested_object");
    sb.start_object();
    sb_field(sb, "v3s", true);
    sb.start_array();
    for(std::size_t i = 0; i < obj.another_object.nested_object.v3s.size(); ++i) {
        sb_next(sb, i == 0);
        auto& v3 = obj.another_object.nested_object.v3s[i];
        sb.start_array();
        sb.append(v3[0]);
        sb.append_comma();
        sb.append(v3[1]);
        sb.append_comma();
        sb.append(v3[2]);
        sb.end_array();
    }
    sb.end_array();
    sb_field(sb, "id");
    sb.escape_and_append_with_quotes(obj.another_object.nested_object.id);
    sb.end_object();
    sb.end_object();

    sb_field(sb, "string_array");
    sb.start_array();
    for(std::size_t i = 0; i < obj.string_array.size(); ++i) {
        sb_next(sb, i == 0);
        sb.escape_and_append_with_quotes(obj.string_array[i]);
    }
    sb.end_array();

    sb_field(sb, "string");
    sb.escape_and_append_with_quotes(obj.string);
    sb_field(sb, "number");
    sb.append(obj.number);
    sb_field(sb, "boolean");
    sb.append(obj.boolean);
    sb_field(sb, "another_bool");
    sb.append(obj.another_bool);

    sb.end_object();
}

// -- benchmarks --

void BM_read(benchmark::State& state) {
    simdjson::padded_string padded(json_minified);
    auto len = json_minified.size();
    obj_t out{};
    for(auto _: state) {
        if(auto r = from_json(static_cast<simdjson::padded_string_view>(padded), out); !r) {
            state.SkipWithError("from_json failed");
            break;
        }
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(len));
    state.SetLabel(std::to_string(len) + " B");
}

void BM_read_handwritten(benchmark::State& state) {
    simdjson::padded_string padded(json_minified);
    auto len = json_minified.size();
    obj_t out{};
    for(auto _: state) {
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        (void)parser.iterate(padded).get(doc);
        handwritten_read_obj(doc, out);
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(len));
    state.SetLabel(std::to_string(len) + " B");
}

void BM_write(benchmark::State& state) {
    auto parsed = from_json<obj_t>(json_minified);
    if(!parsed) {
        state.SkipWithError("setup: from_json failed");
        return;
    }
    auto obj = *parsed;
    auto sample = to_json(obj);
    if(!sample) {
        state.SkipWithError("setup: to_json failed");
        return;
    }
    auto len = sample->size();
    Serializer<> serializer(len);
    for(auto _: state) {
        serializer.clear();
        kota::codec::serialize(serializer, obj);
        benchmark::DoNotOptimize(serializer.view());
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(len));
    state.SetLabel(std::to_string(len) + " B");
}

void BM_write_handwritten(benchmark::State& state) {
    auto parsed = from_json<obj_t>(json_minified);
    if(!parsed) {
        state.SkipWithError("setup: from_json failed");
        return;
    }
    auto obj = *parsed;
    auto sample = to_json(obj);
    if(!sample) {
        state.SkipWithError("setup: to_json failed");
        return;
    }
    auto len = sample->size();
    simdjson::builder::string_builder sb(len);
    for(auto _: state) {
        sb.clear();
        handwritten_write_obj(sb, obj);
        std::string_view result;
        (void)sb.view().get(result);
        benchmark::DoNotOptimize(result);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(len));
    state.SetLabel(std::to_string(len) + " B");
}

BENCHMARK(BM_read);
BENCHMARK(BM_read_handwritten);
BENCHMARK(BM_write);
BENCHMARK(BM_write_handwritten);

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace

BENCHMARK_MAIN();
