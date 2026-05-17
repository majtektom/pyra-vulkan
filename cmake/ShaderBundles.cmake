function(add_shader_bundle bundle_name)
    set(bundle_output_dir ${SHADER_BINARY_ROOT}/${bundle_name})
    file(MAKE_DIRECTORY ${bundle_output_dir})

    set(bundle_outputs)
    foreach(shader_spec IN LISTS ARGN)
        set(compiled_shader_name ${shader_spec})
        set(source_shader_name ${shader_spec})
        set(shader_entry_point main)

        string(FIND "${shader_spec}" "=" spec_equals_index)
        if(NOT spec_equals_index EQUAL -1)
            string(SUBSTRING "${shader_spec}" 0 ${spec_equals_index} compiled_shader_name)
            math(EXPR spec_value_start "${spec_equals_index} + 1")
            string(SUBSTRING "${shader_spec}" ${spec_value_start} -1 source_shader_name)
        endif()

        string(FIND "${source_shader_name}" "@" spec_entry_index)
        if(NOT spec_entry_index EQUAL -1)
            set(source_shader_name_with_entry ${source_shader_name})
            string(SUBSTRING "${source_shader_name_with_entry}" 0 ${spec_entry_index} source_shader_name)
            math(EXPR spec_entry_start "${spec_entry_index} + 1")
            string(SUBSTRING "${source_shader_name_with_entry}" ${spec_entry_start} -1 shader_entry_point)
        endif()

        set(input_file ${SHADER_SOURCE_ROOT}/${bundle_name}/${source_shader_name})
        get_filename_component(shader_language_extension ${compiled_shader_name} LAST_EXT)
        get_filename_component(shader_name_without_language ${compiled_shader_name} NAME_WLE)

        if(shader_language_extension STREQUAL ".vert" OR shader_language_extension STREQUAL ".frag" OR shader_language_extension STREQUAL ".comp")
            set(shader_stage_extension ${shader_language_extension})
            set(shader_source_language glsl)
            set(output_stem ${compiled_shader_name})
        elseif(shader_language_extension STREQUAL ".hlsl" OR shader_language_extension STREQUAL ".slang")
            get_filename_component(shader_stage_extension ${shader_name_without_language} LAST_EXT)
            set(shader_source_language ${shader_language_extension})
            set(output_stem ${shader_name_without_language})
        else()
            message(FATAL_ERROR "Unsupported shader language extension: ${compiled_shader_name}")
        endif()

        if(shader_stage_extension STREQUAL ".vert")
            set(shader_stage vert)
            set(slang_stage vertex)
            set(hlsl_profile vs_6_0)
        elseif(shader_stage_extension STREQUAL ".frag")
            set(shader_stage frag)
            set(slang_stage fragment)
            set(hlsl_profile ps_6_0)
        elseif(shader_stage_extension STREQUAL ".comp")
            set(shader_stage comp)
            set(slang_stage compute)
            set(hlsl_profile cs_6_0)
        else()
            message(FATAL_ERROR "Unsupported shader stage extension: ${compiled_shader_name}")
        endif()

        set(output_file ${bundle_output_dir}/${output_stem}.spv)

        if(shader_source_language STREQUAL "glsl")
            if(GLSL_SHADER_COMPILER_MODE STREQUAL "glslc")
                add_custom_command(
                    OUTPUT ${output_file}
                    COMMAND ${GLSL_SHADER_COMPILER} ${input_file} -o ${output_file}
                    DEPENDS ${input_file}
                    VERBATIM
                )
            else()
                add_custom_command(
                    OUTPUT ${output_file}
                    COMMAND ${GLSL_SHADER_COMPILER} -V ${input_file} -S ${shader_stage} -o ${output_file}
                    DEPENDS ${input_file}
                    VERBATIM
                )
            endif()
        elseif(shader_source_language STREQUAL ".hlsl")
            if(DXC_EXECUTABLE)
                add_custom_command(
                    OUTPUT ${output_file}
                    COMMAND ${DXC_EXECUTABLE} -spirv -T ${hlsl_profile} -E ${shader_entry_point} -Fo ${output_file} ${input_file}
                    DEPENDS ${input_file}
                    VERBATIM
                )
            elseif(GLSLANG_VALIDATOR_EXECUTABLE)
                add_custom_command(
                    OUTPUT ${output_file}
                    COMMAND ${GLSLANG_VALIDATOR_EXECUTABLE} -V -D ${input_file} -S ${shader_stage} -e ${shader_entry_point} -o ${output_file}
                    DEPENDS ${input_file}
                    VERBATIM
                )
            else()
                message(FATAL_ERROR "HLSL shader ${compiled_shader_name} requires dxc or glslangValidator in PATH")
            endif()
        elseif(shader_source_language STREQUAL ".slang")
            if(NOT SLANGC_EXECUTABLE)
                message(FATAL_ERROR "Slang shader ${compiled_shader_name} requires slangc in PATH")
            endif()

            add_custom_command(
                OUTPUT ${output_file}
                COMMAND ${SLANGC_EXECUTABLE} ${input_file} -entry ${shader_entry_point} -stage ${slang_stage} -target spirv -o ${output_file}
                DEPENDS ${input_file}
                VERBATIM
            )
        else()
            message(FATAL_ERROR "Unhandled shader source language for ${compiled_shader_name}")
        endif()

        list(APPEND bundle_outputs ${output_file})
    endforeach()

    add_custom_target(${bundle_name}_shaders DEPENDS ${bundle_outputs})
    add_dependencies(compile_shaders ${bundle_name}_shaders)

    set(${bundle_name}_SHADER_DIR ${bundle_output_dir} PARENT_SCOPE)
    set(${bundle_name}_SHADER_TARGET ${bundle_name}_shaders PARENT_SCOPE)
endfunction()
