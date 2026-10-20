#pragma once
// JK23 rdSprite.type exists only *during* rdSprite_NewEntry(): sithSprite_Load
// immediately replaces +0x20 with the World SPRITES array index. Preserve the
// authored type by sprite resource identity, not by the overwritten field.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace jkgm {
    class sprite_type_registry {
    public:
        void remember(void const *sprite, uint32_t authored_type,
                      char const *loaded_name)
        {
            if(!sprite || !loaded_name) return;
            entry data{};
            data.type=authored_type;
            std::memcpy(data.name.data(),loaded_name,data.name.size());
            types_[sprite]=data;
        }

        bool lookup(void const *sprite,char const *loaded_name,uint32_t &type) const
        {
            if(!sprite || !loaded_name) return false;
            auto const found=types_.find(sprite);
            if(found==types_.end() ||
               std::memcmp(found->second.name.data(),loaded_name,
                           found->second.name.size())!=0) return false;
            type=found->second.type;
            return true;
        }

        void forget(void const *sprite) { types_.erase(sprite); }
        void clear() {types_.clear();}
        size_t size() const {return types_.size();}
    private:
        struct entry {
            uint32_t type=0;
            std::array<char,32> name{};
        };
        std::unordered_map<void const *,entry> types_;
    };
}
