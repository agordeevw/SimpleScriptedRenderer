find_path(GLM_INCLUDE_DIR
	NAMES glm
	PATHS "${CMAKE_CURRENT_LIST_DIR}/include"
	PATH_SUFFIXES glm
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)
set(GLM_INCLUDE_DIRS ${GLM_INCLUDE_DIR})

mark_as_advanced(GLM_INCLUDE_DIR)