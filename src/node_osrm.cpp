#include "json_v8_renderer.hpp"

// v8
#include <nan.h>

// OSRM
#include <osrm/route_parameters.hpp>
#include <osrm/json_container.hpp>
#include <osrm/libosrm_config.hpp>
#include <osrm/osrm.hpp>

#include <boost/optional.hpp>

// STL
#include <iostream>
#include <memory>

namespace node_osrm
{

using osrm_ptr = std::unique_ptr<OSRM>;
using libosrm_config_ptr = std::unique_ptr<LibOSRMConfig>;
using route_parameters_ptr = std::unique_ptr<RouteParameters>;
namespace
{
template <class T, class... Types> std::unique_ptr<T> make_unique(Types &&... Args)
{
    return (std::unique_ptr<T>(new T(std::forward<Types>(Args)...)));
}
}

// Supports
libosrm_config_ptr argumentsToLibOSRMConfig(const Nan::FunctionCallbackInfo<v8::Value> &args)
{
    auto lib_config = make_unique<LibOSRMConfig>();

    if (args.Length() == 0)
    {
        return lib_config;
    }
    else if (args.Length() > 1)
    {
        return libosrm_config_ptr();
    }

    BOOST_ASSERT(args.Length() == 1);

    if (args[0]->IsString())
    {
        lib_config->server_paths["base"] =
            *v8::String::Utf8Value(Nan::To<v8::String>(args[0]).ToLocalChecked());
        lib_config->use_shared_memory = false;
        return lib_config;
    }
    else if (!args[0]->IsObject())
    {
        Nan::ThrowError("parameter must be path or options object.");
        return libosrm_config_ptr();
    }

    BOOST_ASSERT(args[0]->IsObject());
    auto params = Nan::To<v8::Object>(args[0]).ToLocalChecked();

    auto path = params->Get(Nan::New("path").ToLocalChecked());
    auto shared_memory = params->Get(Nan::New("shared_memory").ToLocalChecked());
    if (!path->IsUndefined())
    {
        lib_config->server_paths["base"] =
            *v8::String::Utf8Value(Nan::To<v8::String>(path).ToLocalChecked());
    }
    if (!shared_memory->IsUndefined())
    {
        lib_config->use_shared_memory = Nan::To<bool>(shared_memory).FromJust();
    }

    if (path->IsUndefined() && !lib_config->use_shared_memory)
    {
        Nan::ThrowError("shared_memory must be enabled if no path is "
                        "specified");
        return libosrm_config_ptr();
    }

    return lib_config;
}

boost::optional<std::vector<FixedPointCoordinate>> parseCoordinateArray(const v8::Local<v8::Array> &coordinates_array)
{
    boost::optional<std::vector<FixedPointCoordinate>> resulting_coordinates;
    std::vector<FixedPointCoordinate> temp_coordinates;

    for (uint32_t i = 0; i < coordinates_array->Length(); ++i)
    {
        v8::Local<v8::Value> coordinate = coordinates_array->Get(i);

        if (!coordinate->IsArray())
        {
            Nan::ThrowError("coordinates must be an array of (lat/long) pairs");
            return resulting_coordinates;
        }

        v8::Local<v8::Array> coordinate_pair = v8::Local<v8::Array>::Cast(coordinate);
        if (coordinate_pair->Length() != 2)
        {
            Nan::ThrowError("coordinates must be an array of (lat/long) pairs");
            return resulting_coordinates;
        }

        temp_coordinates.emplace_back(
            static_cast<int>(coordinate_pair->Get(0)->NumberValue() * COORDINATE_PRECISION),
            static_cast<int>(coordinate_pair->Get(1)->NumberValue() * COORDINATE_PRECISION));
    }
    resulting_coordinates =
        boost::make_optional<std::vector<FixedPointCoordinate>>(std::move(temp_coordinates));
    return resulting_coordinates;
}

// Parses all the non-service specific parameters
route_parameters_ptr argumentsToParameter(const Nan::FunctionCallbackInfo<v8::Value> &args)
{
    auto params = make_unique<RouteParameters>();

    if (args.Length() < 2)
    {
        Nan::ThrowTypeError("two arguments required");
        return route_parameters_ptr();
    }

    if (!args[0]->IsObject())
    {
        Nan::ThrowTypeError("first arg must be an object");
        return route_parameters_ptr();
    }

    v8::Local<v8::Object> obj = Nan::To<v8::Object>(args[0]).ToLocalChecked();

    v8::Local<v8::Value> coordinates = obj->Get(Nan::New("coordinates").ToLocalChecked());
    if (coordinates->IsUndefined())
    {
        Nan::ThrowError("must provide a coordinates property");
        return route_parameters_ptr();
    }
    else if (coordinates->IsArray())
    {
        auto coordinates_array = v8::Local<v8::Array>::Cast(coordinates);
        if (coordinates_array->Length() < 2)
        {
            Nan::ThrowError("at least two coordinates must be provided");
            return route_parameters_ptr();
        }
        auto maybe_coordinates = parseCoordinateArray(coordinates_array);
        if (maybe_coordinates)
        {
            std::copy(maybe_coordinates->begin(), maybe_coordinates->end(), std::back_inserter(params->coordinates));
            params->is_source.insert(params->is_source.end(), maybe_coordinates->size(), true);
            params->is_destination.insert(params->is_destination.end(), maybe_coordinates->size(), true);
        }
        else
        {
            return route_parameters_ptr();
        }
    }
    else
    {
        Nan::ThrowError("coordinates must be an array of (lat/long) pairs");
        return route_parameters_ptr();
    }

    if (obj->Has(Nan::New("alternateRoute").ToLocalChecked()))
    {
        params->alternate_route =
            obj->Get(Nan::New("alternateRoute").ToLocalChecked())->BooleanValue();
    }

    if (obj->Has(Nan::New("checksum").ToLocalChecked()))
    {
        params->check_sum =
            static_cast<unsigned>(obj->Get(Nan::New("checksum").ToLocalChecked())->Uint32Value());
    }

    if (obj->Has(Nan::New("zoomLevel").ToLocalChecked()))
    {
        params->zoom_level =
            static_cast<short>(obj->Get(Nan::New("zoomLevel").ToLocalChecked())->Int32Value());
    }

    if (obj->Has(Nan::New("printInstructions").ToLocalChecked()))
    {
        params->print_instructions =
            obj->Get(Nan::New("printInstructions").ToLocalChecked())->BooleanValue();
    }

    if (obj->Has(Nan::New("geometry").ToLocalChecked()))
    {
        params->geometry = obj->Get(Nan::New("geometry").ToLocalChecked())->BooleanValue();
    }

    if (obj->Has(Nan::New("compression").ToLocalChecked()))
    {
        params->compression = obj->Get(Nan::New("compression").ToLocalChecked())->BooleanValue();
    }

    if (obj->Has(Nan::New("hints").ToLocalChecked()))
    {
        v8::Local<v8::Value> hints = obj->Get(Nan::New("hints").ToLocalChecked());

        if (!hints->IsArray())
        {
            Nan::ThrowError("hints must be an array of strings/null");
            return route_parameters_ptr();
        }

        v8::Local<v8::Array> hints_array = v8::Local<v8::Array>::Cast(hints);
        for (uint32_t i = 0; i < hints_array->Length(); ++i)
        {
            v8::Local<v8::Value> hint = hints_array->Get(i);
            if (hint->IsString())
            {
                params->hints.push_back(*v8::String::Utf8Value(hint));
            }
            else if (hint->IsNull())
            {
                params->hints.push_back("");
            }
            else
            {
                Nan::ThrowError("hint must be null or string");
                return route_parameters_ptr();
            }
        }
    }

    BOOST_ASSERT(params->is_source.size() == params->is_destination.size());
    BOOST_ASSERT(params->coordinates.size() == params->is_destination.size());

    return params;
}

class Engine final : public Nan::ObjectWrap
{
  public:
    static void Initialize(v8::Handle<v8::Object>);
    static void New(const Nan::FunctionCallbackInfo<v8::Value> &args);

    static void route(const Nan::FunctionCallbackInfo<v8::Value> &args);
    static void nearest(const Nan::FunctionCallbackInfo<v8::Value> &args);
    static void table(const Nan::FunctionCallbackInfo<v8::Value> &args);
    static void match(const Nan::FunctionCallbackInfo<v8::Value> &args);
    static void trip(const Nan::FunctionCallbackInfo<v8::Value> &args);

    static void Run(const Nan::FunctionCallbackInfo<v8::Value> &args, route_parameters_ptr);
    static void AsyncRun(uv_work_t *);
    static void AfterRun(uv_work_t *);

  private:
    Engine(LibOSRMConfig lib_config)
        : Nan::ObjectWrap(), this_(make_unique<OSRM>(std::move(lib_config)))
    {
    }

    static Nan::Persistent<v8::Function> constructor;
    osrm_ptr this_;
};

Nan::Persistent<v8::Function> Engine::constructor;

void Engine::Initialize(v8::Handle<v8::Object> target)
{
    v8::Local<v8::FunctionTemplate> function_template = Nan::New<v8::FunctionTemplate>(Engine::New);

    function_template->InstanceTemplate()->SetInternalFieldCount(1);
    function_template->SetClassName(Nan::New("OSRM").ToLocalChecked());

    SetPrototypeMethod(function_template, "route", route);
    SetPrototypeMethod(function_template, "nearest", nearest);
    SetPrototypeMethod(function_template, "table", table);
    SetPrototypeMethod(function_template, "match", match);
    SetPrototypeMethod(function_template, "trip", trip);

    constructor.Reset(function_template->GetFunction());
    Nan::Set(target, Nan::New("OSRM").ToLocalChecked(), function_template->GetFunction());
}

void Engine::New(const Nan::FunctionCallbackInfo<v8::Value> &args)
{
    if (!args.IsConstructCall())
    {
        Nan::ThrowTypeError("Cannot call constructor as function, you need to use 'new' "
                            "keyword");
        return;
    }

    try
    {
        auto lib_config_ptr = argumentsToLibOSRMConfig(args);
        if (!lib_config_ptr)
        {
            return;
        }
        auto engine_ptr = new Engine(std::move(*lib_config_ptr));
        engine_ptr->Wrap(args.This());
        args.GetReturnValue().Set(args.This());
    }
    catch (std::exception const &ex)
    {
        Nan::ThrowTypeError(ex.what());
    }
}

struct RunQueryBaton
{
    uv_work_t request;
    Engine *machine;
    osrm::json::Object result;
    route_parameters_ptr params;
    Nan::Persistent<v8::Function> cb;
    std::string error;
};

void Engine::route(const Nan::FunctionCallbackInfo<v8::Value> &args)
{
    auto params = argumentsToParameter(args);
    if (!params)
        return;

    params->service = "viaroute";

    Run(args, std::move(params));
}

void Engine::match(const Nan::FunctionCallbackInfo<v8::Value> &args)
{
    auto params = argumentsToParameter(args);
    if (!params)
        return;

    v8::Local<v8::Object> obj = Nan::To<v8::Object>(args[0]).ToLocalChecked();

    v8::Local<v8::Value> timestamps = obj->Get(Nan::New("timestamps").ToLocalChecked());
    if (!timestamps->IsArray() && !timestamps->IsUndefined())
    {
        Nan::ThrowError("timestamps must be an array of integers (or undefined)");
        return;
    }

    if (obj->Has(Nan::New("classify").ToLocalChecked()))
    {
        params->classify = obj->Get(Nan::New("classify").ToLocalChecked())->BooleanValue();
    }
    if (obj->Has(Nan::New("gps_precision").ToLocalChecked()))
    {
        params->gps_precision = obj->Get(Nan::New("gps_precision").ToLocalChecked())->NumberValue();
    }
    if (obj->Has(Nan::New("matching_beta").ToLocalChecked()))
    {
        params->matching_beta = obj->Get(Nan::New("matching_beta").ToLocalChecked())->NumberValue();
    }

    if (timestamps->IsArray())
    {
        v8::Local<v8::Array> timestamps_array = v8::Local<v8::Array>::Cast(timestamps);
        if (params->coordinates.size() != timestamps_array->Length())
        {
            Nan::ThrowError("timestamp array must have the same size as the coordinates "
                            "array");
            return;
        }

        // add all timestamps
        for (uint32_t i = 0; i < timestamps_array->Length(); ++i)
        {
            if (!timestamps_array->Get(i)->IsNumber())
            {
                Nan::ThrowError("timestamps array items must be numbers");
                return;
            }
            params->timestamps.emplace_back(
                static_cast<unsigned>(timestamps_array->Get(i)->NumberValue()));
        }
    }

    params->service = "match";

    Run(args, std::move(params));
}

// uses the same options as viaroute
void Engine::trip(const Nan::FunctionCallbackInfo<v8::Value> &args)
{
    auto params = argumentsToParameter(args);
    if (!params)
        return;

    params->service = "trip";

    Run(args, std::move(params));
}

void Engine::table(const Nan::FunctionCallbackInfo<v8::Value> &args)
{
    auto params = argumentsToParameter(args);
    if (!params)
        return;

    v8::Local<v8::Object> obj = Nan::To<v8::Object>(args[0]).ToLocalChecked();

    v8::Local<v8::Value> sources = obj->Get(Nan::New("sources").ToLocalChecked());
    if (!sources->IsArray() && !sources->IsUndefined())
    {
        Nan::ThrowError("sources must be an array of coordinates (or undefined)");
        return;
    }
    v8::Local<v8::Value> destinations = obj->Get(Nan::New("destinations").ToLocalChecked());
    if (!destinations->IsArray() && !destinations->IsUndefined())
    {
        Nan::ThrowError("destinations must be an array of coordinates (or undefined)");
        return;
    }

    if (destinations->IsUndefined() != sources->IsUndefined())
    {
        Nan::ThrowError("Both sources and destinations need to be specified");
        return;
    }

    if (!destinations->IsUndefined() && !sources->IsUndefined() && params->coordinates.size() > 0)
    {
        Nan::ThrowError("You can either specifiy sources and destinations, or coordinates");
        return;
    }

    if (sources->IsArray())
    {
        auto sources_array = v8::Local<v8::Array>::Cast(sources);
        auto maybe_sources = parseCoordinateArray(sources_array);
        if (maybe_sources)
        {
            std::copy(maybe_sources->begin(), maybe_sources->end(), std::back_inserter(params->coordinates));
            params->is_source.insert(params->is_source.end(), maybe_sources->size(), true);
            params->is_destination.insert(params->is_destination.end(), maybe_sources->size(), false);
        }
        else
        {
            return;
        }
    }
    if (destinations->IsArray())
    {
        auto destinations_array = v8::Local<v8::Array>::Cast(destinations);
        auto maybe_destinations = parseCoordinateArray(destinations_array);
        if (maybe_destinations)
        {
            std::copy(maybe_destinations->begin(), maybe_destinations->end(), std::back_inserter(params->coordinates));
            params->is_source.insert(params->is_source.end(), maybe_destinations->size(), false);
            params->is_destination.insert(params->is_destination.end(), maybe_destinations->size(), true);
        }
        else
        {
            return;
        }
    }

    BOOST_ASSERT(params->is_source.size() == params->is_destination.size());
    BOOST_ASSERT(params->coordinates.size() == params->is_destination.size());

    params->service = "table";

    Run(args, std::move(params));
}

void Engine::nearest(const Nan::FunctionCallbackInfo<v8::Value> &args)
{
    if (args.Length() < 2)
    {
        Nan::ThrowTypeError("two arguments required");
        return;
    }

    v8::Local<v8::Value> coordinate = args[0];
    if (!coordinate->IsArray())
    {
        Nan::ThrowError("first argument must be an array of lat, long");
        return;
    }

    v8::Local<v8::Array> coordinate_pair = v8::Local<v8::Array>::Cast(coordinate);
    if (coordinate_pair->Length() != 2)
    {
        Nan::ThrowError("first argument must be an array of lat, long");
        return;
    }

    route_parameters_ptr params = make_unique<RouteParameters>();

    params->service = "nearest";
    params->coordinates.emplace_back(
        static_cast<int>(coordinate_pair->Get(0)->NumberValue() * COORDINATE_PRECISION),
        static_cast<int>(coordinate_pair->Get(1)->NumberValue() * COORDINATE_PRECISION));
    Run(args, std::move(params));
}

void Engine::Run(const Nan::FunctionCallbackInfo<v8::Value> &args, route_parameters_ptr params)
{
    v8::Local<v8::Value> callback = args[args.Length() - 1];

    if (!callback->IsFunction())
    {
        Nan::ThrowTypeError("last argument must be a callback function");
        return;
    }

    auto closure = new RunQueryBaton();
    closure->request.data = closure;
    closure->machine = ObjectWrap::Unwrap<Engine>(args.This());
    closure->params = std::move(params);
    closure->error = "";
    closure->cb.Reset(callback.As<v8::Function>());
    uv_queue_work(uv_default_loop(), &closure->request, AsyncRun,
                  reinterpret_cast<uv_after_work_cb>(AfterRun));
    closure->machine->Ref();
    return;
}

void Engine::AsyncRun(uv_work_t *req)
{
    RunQueryBaton *closure = static_cast<RunQueryBaton *>(req->data);
    try
    {
        closure->machine->this_->RunQuery(*closure->params, closure->result);
    }
    catch (std::exception const &ex)
    {
        closure->error = ex.what();
    }
}

void Engine::AfterRun(uv_work_t *req)
{
    RunQueryBaton *closure = static_cast<RunQueryBaton *>(req->data);
    Nan::TryCatch try_catch;
    if (closure->error.size() > 0)
    {
        v8::Local<v8::Value> argv[1] = {Nan::Error(closure->error.c_str())};
        Nan::MakeCallback(Nan::GetCurrentContext()->Global(), Nan::New(closure->cb), 1, argv);
    }
    else
    {
        v8::Local<v8::Value> result;
        osrm::json::render(result, closure->result);
        v8::Local<v8::Value> argv[2] = {Nan::Null(), result};
        Nan::MakeCallback(Nan::GetCurrentContext()->Global(), Nan::New(closure->cb), 2, argv);
    }
    if (try_catch.HasCaught())
    {
        Nan::FatalException(try_catch);
    }
    closure->machine->Unref();
    closure->cb.Reset();
    delete closure;
}

extern "C" {
static void start(v8::Handle<v8::Object> target) { Engine::Initialize(target); }
}

} // namespace node_osrm

NODE_MODULE(osrm, node_osrm::start)
