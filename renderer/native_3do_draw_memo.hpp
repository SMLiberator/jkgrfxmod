#pragma once
#include <cstddef>
#include <vector>
// Native draw-local data must be captured lazily AFTER rdModel3_Draw
// selects its camera, GeoSet and effective geometry/lighting modes.
namespace jkgm {
template<class T> struct native_draw_memo {
    bool ready=false;
    T value{};
    template<class Read> T get(Read const &read) {
        if(!ready) { value=read();ready=true; }
        return value;
    }
};
// A draw-local list is lazy, and returned by reference rather than copying
// all camera lights for every hierarchy node. Scope invalidation discards
// it together with the camera header. No allocation for zero-light models.
template<class T> struct native_draw_list_memo {
    bool ready=false;
    std::vector<T> values;
    template<class Read>
    std::vector<T> const &get(std::size_t count,Read const &read) {
        if(!ready || values.size()!=count) {
            values.clear();
            values.reserve(count);
            for(std::size_t i=0;i<count;++i)values.push_back(read(i));
            ready=true;
        }
        return values;
    }
};
template<class State> struct native_draw_scope {
    State *&active;
    State *previous;
    State state{};
    native_draw_scope(State *&slot,bool enabled)
        :active(slot),previous(slot) { active=enabled?&state:nullptr; }
    ~native_draw_scope() {
        active=previous;
        // A nested engine draw may have changed the native globals.
        if(previous)*previous=State{};
    }
    native_draw_scope(native_draw_scope const &)=delete;
    native_draw_scope &operator=(native_draw_scope const &)=delete;
};
template<class State,class T,class Read>
T native_draw_read(State *active,native_draw_memo<T> State::*field,
                   Read const &read) {
    return active?(active->*field).get(read):read();
}
}
