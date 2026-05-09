function main(target)
    local hal_root = target:values("hal_root")
    if not hal_root then
        hal_root = "bsp/HAL"
    end

    local file = io.open(path.join(hal_root, "Makefile"), "r")
    if (file ~= nil) then
        local text = file:read("a"):gsub("\r", ""):gsub("\\ *\n", " ")
        text:match("C_SOURCES *=([^\r\n\t\v\f]+)\n"):gsub("[^ ]+", function(f)
            target:add("files", path.join(hal_root, f))
        end)
        text:match("ASM_SOURCES *=([^\r\n\t\v\f]+)\n"):gsub("[^ ]+", function(f)
            target:add("files", path.join(hal_root, f))
        end)
        text:match("C_INCLUDES *=([^\r\n\t\v\f]+)\n"):gsub("-I([^ ]+)", function(f)
            target:add("includedirs", path.join(hal_root, f))
        end)
    end
end
