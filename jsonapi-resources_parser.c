#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <json-c/json.h>

#define MAX_LINE_LENGTH 1024
#define MAX_PATH_LENGTH 512
#define MAX_ATTRIBUTES 100
#define MAX_FILTERS 50
#define MAX_RELATIONS 20

typedef struct {
    char name[64];
    char type[32];
    char collection[128];
} Filter;

typedef struct {
    char name[64];
    char relation_name[64];
    char foreign_key_on[32];
    char type[16]; // "has_one", "has_many"
} Relation;

typedef struct {
    char class_name[128];
    char model_name[64];
    char create_form[128];
    char attributes[MAX_ATTRIBUTES][64];
    int attribute_count;
    Filter filters[MAX_FILTERS];
    int filter_count;
    Relation relations[MAX_RELATIONS];
    int relation_count;
    char creatable_fields[MAX_ATTRIBUTES][64];
    int creatable_count;
    char updatable_fields[MAX_ATTRIBUTES][64];
    int updatable_count;
    char paginator[32];
    char default_sort_field[64];
    char default_sort_direction[8];
} ResourceInfo;

typedef struct {
    char path[128];
    char controller[128];
    char action[32];
    char method[16];  // Increased from 8 to 16 to safely hold "GET|POST"
    char resource_name[64];
} RouteInfo;

typedef struct {
    RouteInfo routes[100];
    int route_count;
    ResourceInfo resources[50];
    int resource_count;
} ApiSpec;

// Utility functions
char* trim_whitespace(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

char* extract_quoted_string(const char* line, const char* pattern) {
    char* start = strstr(line, pattern);
    if (!start) return NULL;

    start = strchr(start, '\'');
    if (!start) start = strchr(strstr(line, pattern), '"');
    if (!start) return NULL;

    start++;
    char* end = strchr(start, start[-1]);
    if (!end) return NULL;

    int len = end - start;
    char* result = malloc(len + 1);
    strncpy(result, start, len);
    result[len] = '\0';
    return result;
}

void parse_attributes_line(const char* line, ResourceInfo* resource) {
    const char* start = strstr(line, "attributes");
    if (!start) return;

    start = strchr(start, ':');
    if (!start) return;

    char* line_copy = strdup(line);
    char* token = strtok(line_copy, " :,");
    int found_attributes = 0;

    while (token != NULL && resource->attribute_count < MAX_ATTRIBUTES) {
        if (found_attributes && token[0] != '\0' && strcmp(token, "attributes") != 0) {
            // Clean the token
            char* clean_token = trim_whitespace(token);
            if (strlen(clean_token) > 0 && clean_token[0] == ':') {
                clean_token++;
            }
            if (strlen(clean_token) > 0) {
                strncpy(resource->attributes[resource->attribute_count], clean_token, 63);
                resource->attributes[resource->attribute_count][63] = '\0';
                resource->attribute_count++;
            }
        }
        if (strcmp(token, "attributes") == 0) {
            found_attributes = 1;
        }
        token = strtok(NULL, " :,");
    }

    free(line_copy);
}

void parse_filter_line(const char* line, ResourceInfo* resource) {
    if (strstr(line, "ransack_filter") || strstr(line, "association_uuid_filter") ||
        (strstr(line, "filter") && !strstr(line, "def"))) {

        if (resource->filter_count >= MAX_FILTERS) return;

        Filter* filter = &resource->filters[resource->filter_count];
        memset(filter, 0, sizeof(Filter)); // Initialize the filter structure

        // Extract filter name
        char* name_start = strchr(line, ':');
        if (name_start) {
            name_start++;
            char* name_end = strchr(name_start, ',');
            if (!name_end) name_end = strchr(name_start, '\n');
            if (!name_end) name_end = name_start + strlen(name_start);

            size_t name_len = name_end - name_start;
            if (name_len > 0 && name_len < sizeof(filter->name)) {
                strncpy(filter->name, name_start, name_len);
                filter->name[sizeof(filter->name) - 1] = '\0'; // Ensure null termination
                // Remove trailing whitespace
                char* p = filter->name + strcspn(filter->name, " \t\n\r");
                *p = '\0';
            }
        }

        // Extract type
        char* type_str = strstr(line, "type:");
        if (type_str) {
            char* extracted_type = extract_quoted_string(type_str, "type:");
            if (extracted_type) {
                strncpy(filter->type, extracted_type, sizeof(filter->type) - 1);
                filter->type[sizeof(filter->type) - 1] = '\0';
                free(extracted_type);
            }
        } else if (strstr(line, "association_uuid_filter")) {
            strncpy(filter->type, "uuid", sizeof(filter->type) - 1);
            filter->type[sizeof(filter->type) - 1] = '\0';
        } else {
            strncpy(filter->type, "string", sizeof(filter->type) - 1);
            filter->type[sizeof(filter->type) - 1] = '\0';
        }

        // Extract collection
        char* collection_str = strstr(line, "collection:");
        if (collection_str) {
            char* extracted_collection = extract_quoted_string(collection_str, "collection:");
            if (extracted_collection) {
                strncpy(filter->collection, extracted_collection, sizeof(filter->collection) - 1);
                filter->collection[sizeof(filter->collection) - 1] = '\0';
                free(extracted_collection);
            }
        }

        resource->filter_count++;
    }
}

void parse_relation_line(const char* line, ResourceInfo* resource) {
    if (strstr(line, "has_one") || strstr(line, "has_many")) {
        if (resource->relation_count >= MAX_RELATIONS) return;

        Relation* relation = &resource->relations[resource->relation_count];

        // Determine relation type
        if (strstr(line, "has_one")) {
            strcpy(relation->type, "has_one");
        } else {
            strcpy(relation->type, "has_many");
        }

        // Extract relation name
        char* name_start = strchr(line, ':');
        if (name_start) {
            name_start++;
            char* name_end = strchr(name_start, ',');
            if (!name_end) name_end = strchr(name_start, '\n');
            if (!name_end) name_end = name_start + strlen(name_start);

            int name_len = name_end - name_start;
            if (name_len > 0 && name_len < 64) {
                strncpy(relation->name, name_start, name_len);
                relation->name[name_len] = '\0';
                relation->name[strcspn(relation->name, " \t\n")] = '\0';
            }
        }

        // Extract relation_name
        char* relation_name_str = strstr(line, "relation_name:");
        if (relation_name_str) {
            char* extracted_name = extract_quoted_string(relation_name_str, "relation_name:");
            if (extracted_name) {
                strncpy(relation->relation_name, extracted_name, 63);
                relation->relation_name[63] = '\0';
                free(extracted_name);
            }
        }

        // Extract foreign_key_on
        char* foreign_key_str = strstr(line, "foreign_key_on:");
        if (foreign_key_str) {
            char* extracted_key = extract_quoted_string(foreign_key_str, "foreign_key_on:");
            if (extracted_key) {
                strncpy(relation->foreign_key_on, extracted_key, 31);
                relation->foreign_key_on[31] = '\0';
                free(extracted_key);
            }
        }

        resource->relation_count++;
    }
}

void parse_resource_file(const char* filename, ResourceInfo* resource) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Cannot open file %s\n", filename);
        return;
    }

    char line[MAX_LINE_LENGTH];
    int in_creatable_fields = 0;
    int in_updatable_fields = 0;

    // Extract class name from filename
    const char* basename = strrchr(filename, '/');
    if (basename) basename++;
    else basename = filename;

    strncpy(resource->class_name, basename, 127);
    resource->class_name[127] = '\0';

    // Remove .rb extension
    char* ext = strstr(resource->class_name, ".rb");
    if (ext) *ext = '\0';

    while (fgets(line, sizeof(line), file)) {
        char* trimmed_line = trim_whitespace(line);

        // Parse model_name
        if (strstr(trimmed_line, "model_name")) {
            char* model_name = extract_quoted_string(trimmed_line, "model_name");
            if (model_name) {
                strncpy(resource->model_name, model_name, 63);
                resource->model_name[63] = '\0';
                free(model_name);
            }
        }

        // Parse create_form
        if (strstr(trimmed_line, "create_form")) {
            char* create_form = extract_quoted_string(trimmed_line, "create_form");
            if (create_form) {
                strncpy(resource->create_form, create_form, 127);
                resource->create_form[127] = '\0';
                free(create_form);
            }
        }

        // Parse paginator
        if (strstr(trimmed_line, "paginator")) {
            char* paginator = extract_quoted_string(trimmed_line, "paginator");
            if (paginator) {
                strncpy(resource->paginator, paginator, 31);
                resource->paginator[31] = '\0';
                free(paginator);
            } else if (strstr(trimmed_line, ":paged")) {
                strcpy(resource->paginator, "paged");
            }
        }

        // Parse default_sort
        if (strstr(trimmed_line, "default_sort") && strstr(trimmed_line, "field")) {
            char* field_str = strstr(trimmed_line, "field:");
            if (field_str) {
                char* field = extract_quoted_string(field_str, "field:");
                if (field) {
                    strncpy(resource->default_sort_field, field, 63);
                    resource->default_sort_field[63] = '\0';
                    free(field);
                }
            }

            char* direction_str = strstr(trimmed_line, "direction:");
            if (direction_str) {
                if (strstr(direction_str, ":desc")) {
                    strcpy(resource->default_sort_direction, "desc");
                } else if (strstr(direction_str, ":asc")) {
                    strcpy(resource->default_sort_direction, "asc");
                }
            }
        }

        // Parse attributes
        if (strstr(trimmed_line, "attributes") && !strstr(trimmed_line, "def")) {
            parse_attributes_line(trimmed_line, resource);
        }

        // Parse filters
        parse_filter_line(trimmed_line, resource);

        // Parse relations
        parse_relation_line(trimmed_line, resource);

        // Parse creatable_fields
        if (strstr(trimmed_line, "def self.creatable_fields")) {
            in_creatable_fields = 1;
        } else if (in_creatable_fields && strstr(trimmed_line, "end")) {
            in_creatable_fields = 0;
        } else if (in_creatable_fields && strstr(trimmed_line, "%i[")) {
            // Parse the fields array
            char* start = strstr(trimmed_line, "%i[");
            char* end = strchr(start, ']');
            if (start && end) {
                start += 3; // Skip "%i["
                char* field_str = malloc(end - start + 1);
                strncpy(field_str, start, end - start);
                field_str[end - start] = '\0';

                char* token = strtok(field_str, " ");
                while (token && resource->creatable_count < MAX_ATTRIBUTES) {
                    if (strlen(token) > 0) {
                        strncpy(resource->creatable_fields[resource->creatable_count], token, 63);
                        resource->creatable_fields[resource->creatable_count][63] = '\0';
                        resource->creatable_count++;
                    }
                    token = strtok(NULL, " ");
                }
                free(field_str);
            }
        }

        // Parse updatable_fields (similar logic)
        if (strstr(trimmed_line, "def self.updatable_fields")) {
            in_updatable_fields = 1;
        } else if (in_updatable_fields && strstr(trimmed_line, "end")) {
            in_updatable_fields = 0;
        } else if (in_updatable_fields && strstr(trimmed_line, "%i[")) {
            // Parse the fields array
            char* start = strstr(trimmed_line, "%i[");
            char* end = strchr(start, ']');
            if (start && end) {
                start += 3; // Skip "%i["
                char* field_str = malloc(end - start + 1);
                strncpy(field_str, start, end - start);
                field_str[end - start] = '\0';

                char* token = strtok(field_str, " ");
                while (token && resource->updatable_count < MAX_ATTRIBUTES) {
                    if (strlen(token) > 0) {
                        strncpy(resource->updatable_fields[resource->updatable_count], token, 63);
                        resource->updatable_fields[resource->updatable_count][63] = '\0';
                        resource->updatable_count++;
                    }
                    token = strtok(NULL, " ");
                }
                free(field_str);
            }
        }
    }

    fclose(file);
}

void parse_routes_file(const char* filename, ApiSpec* spec) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Cannot open routes file %s\n", filename);
        return;
    }

    char line[MAX_LINE_LENGTH];
    char namespace_stack[10][64];
    int namespace_depth = 0;

    while (fgets(line, sizeof(line), file)) {
        char* trimmed_line = trim_whitespace(line);

        // Skip empty lines and comments
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') {
            continue;
        }

        // Handle namespace declarations
        if (strstr(trimmed_line, "namespace") && strchr(trimmed_line, ':')) {
            char* namespace_start = strchr(trimmed_line, ':');
            if (namespace_start) {
                namespace_start++;
                char* namespace_end = strstr(namespace_start, " do");
                if (!namespace_end) namespace_end = strchr(namespace_start, '{');
                if (!namespace_end) namespace_end = strchr(namespace_start, ' ');
                if (namespace_end && namespace_depth < 9) {
                    int len = namespace_end - namespace_start;
                    if (len > 0 && len < 63) {
                        strncpy(namespace_stack[namespace_depth], namespace_start, len);
                        namespace_stack[namespace_depth][len] = '\0';
                        // Remove quotes if present
                        if (namespace_stack[namespace_depth][0] == '\'' || namespace_stack[namespace_depth][0] == '"') {
                            memmove(namespace_stack[namespace_depth], namespace_stack[namespace_depth] + 1, len);
                            namespace_stack[namespace_depth][len-2] = '\0';
                        }
                        namespace_depth++;
                    }
                }
            }
        }

        // Handle end statements (close namespace) - be more careful
        if ((strcmp(trimmed_line, "end") == 0 || strstr(trimmed_line, "end ")) && namespace_depth > 0) {
            namespace_depth--;
        }

        // Handle resource declarations
        if (strstr(trimmed_line, "resources") && strchr(trimmed_line, ':')) {
            if (spec->route_count >= 100) continue;

            RouteInfo* route = &spec->routes[spec->route_count];
            memset(route, 0, sizeof(RouteInfo)); // Initialize to zero

            // Extract resource name
            char* resource_start = strchr(trimmed_line, ':');
            if (resource_start) {
                resource_start++;
                char* resource_end = strchr(resource_start, ',');
                if (!resource_end) resource_end = strstr(resource_start, " do");
                if (!resource_end) resource_end = strchr(resource_start, ' ');
                if (!resource_end) resource_end = resource_start + strlen(resource_start);

                int len = resource_end - resource_start;
                if (len > 0 && len < 63) {
                    strncpy(route->resource_name, resource_start, len);
                    route->resource_name[len] = '\0';
                    route->resource_name[strcspn(route->resource_name, " \t\n\r")] = '\0';

                    // Remove quotes if present
                    if (route->resource_name[0] == '\'' || route->resource_name[0] == '"') {
                        memmove(route->resource_name, route->resource_name + 1, strlen(route->resource_name));
                        int name_len = strlen(route->resource_name);
                        if (name_len > 0 && (route->resource_name[name_len-1] == '\'' || route->resource_name[name_len-1] == '"')) {
                            route->resource_name[name_len-1] = '\0';
                        }
                    }

                    // Build full path from namespace stack - check buffer limits
                    strcpy(route->path, "/api");
                    for (int i = 0; i < namespace_depth && strlen(route->path) < 100; i++) {
                        if (strlen(route->path) + strlen(namespace_stack[i]) + 2 < 127) {
                            strcat(route->path, "/");
                            strcat(route->path, namespace_stack[i]);
                        }
                    }
                    if (strlen(route->path) + strlen(route->resource_name) + 2 < 127) {
                        strcat(route->path, "/");
                        strcat(route->path, route->resource_name);
                    }

                    // Set default HTTP methods for RESTful resources
                    strcpy(route->method, "GET|POST");

                    spec->route_count++;
                }
            }
        }
    }

    fclose(file);
}

void scan_resource_files(const char* directory, ApiSpec* spec) {
    DIR* dir = opendir(directory);
    if (!dir) {
        printf("Error: Cannot open directory %s\n", directory);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && spec->resource_count < 50) {
        if (strstr(entry->d_name, "_resource.rb")) {
            char filepath[MAX_PATH_LENGTH];
            snprintf(filepath, sizeof(filepath), "%s/%s", directory, entry->d_name);

            ResourceInfo* resource = &spec->resources[spec->resource_count];
            memset(resource, 0, sizeof(ResourceInfo));

            parse_resource_file(filepath, resource);
            spec->resource_count++;

            printf("Parsed resource: %s\n", resource->class_name);
        }
    }

    closedir(dir);
}

json_object* generate_json_api_spec(const ApiSpec* spec) {
    json_object* root = json_object_new_object();
    if (!root) return NULL;

    json_object* openapi = json_object_new_string("3.0.0");
    json_object_object_add(root, "openapi", openapi);

    // Info section
    json_object* info = json_object_new_object();
    json_object* title = json_object_new_string("Rails JSON:API Specification");
    json_object* version = json_object_new_string("1.0.0");
    json_object_object_add(info, "title", title);
    json_object_object_add(info, "version", version);
    json_object_object_add(root, "info", info);

    // Paths section
    json_object* paths = json_object_new_object();

    for (int i = 0; i < spec->resource_count; i++) {
        const ResourceInfo* resource = &spec->resources[i];

        // Find matching route
        const char* resource_path = NULL;
        char default_path[256];

        for (int j = 0; j < spec->route_count; j++) {
            if (strstr(resource->class_name, spec->routes[j].resource_name) ||
                strstr(spec->routes[j].resource_name, resource->model_name)) {
                resource_path = spec->routes[j].path;
                break;
            }
        }

        if (!resource_path) {
            // Generate default path - be safe with string operations
            int written = snprintf(default_path, sizeof(default_path), "/api/v1/%s",
                                  strlen(resource->model_name) > 0 ? resource->model_name : "resource");
            if (written >= sizeof(default_path)) {
                strcpy(default_path, "/api/v1/resource"); // fallback
            }
            resource_path = default_path;
        }

        json_object* path_obj = json_object_new_object();
        if (!path_obj) continue;

        // GET method (list)
        json_object* get_method = json_object_new_object();
        json_object* get_summary = json_object_new_string("List resources");
        json_object_object_add(get_method, "summary", get_summary);

        // Add parameters for filters
        if (resource->filter_count > 0) {
            json_object* parameters = json_object_new_array();
            for (int f = 0; f < resource->filter_count && f < MAX_FILTERS; f++) {
                json_object* param = json_object_new_object();
                if (!param) continue;

                json_object* param_name = json_object_new_string(resource->filters[f].name);
                json_object* param_in = json_object_new_string("query");
                json_object* param_required = json_object_new_boolean(0);

                json_object* param_schema = json_object_new_object();
                const char* filter_type = strlen(resource->filters[f].type) > 0 ?
                                         resource->filters[f].type : "string";
                json_object* param_type = json_object_new_string(filter_type);
                json_object_object_add(param_schema, "type", param_type);

                json_object_object_add(param, "name", param_name);
                json_object_object_add(param, "in", param_in);
                json_object_object_add(param, "required", param_required);
                json_object_object_add(param, "schema", param_schema);

                json_object_array_add(parameters, param);
            }
            json_object_object_add(get_method, "parameters", parameters);
        }

        json_object_object_add(path_obj, "get", get_method);

        // POST method (create) - only if creatable_fields exist
        if (resource->creatable_count > 0) {
            json_object* post_method = json_object_new_object();
            json_object* post_summary = json_object_new_string("Create resource");
            json_object_object_add(post_method, "summary", post_summary);

            json_object_object_add(path_obj, "post", post_method);
        }

        json_object_object_add(paths, resource_path, path_obj);
    }

    json_object_object_add(root, "paths", paths);

    // Components/Schemas section
    json_object* components = json_object_new_object();
    json_object* schemas = json_object_new_object();

    for (int i = 0; i < spec->resource_count; i++) {
        const ResourceInfo* resource = &spec->resources[i];

        json_object* schema = json_object_new_object();
        if (!schema) continue;

        json_object* schema_type = json_object_new_string("object");
        json_object_object_add(schema, "type", schema_type);

        json_object* properties = json_object_new_object();

        // Add attributes as properties
        for (int a = 0; a < resource->attribute_count && a < MAX_ATTRIBUTES; a++) {
            if (strlen(resource->attributes[a]) > 0) {
                json_object* attr_schema = json_object_new_object();
                json_object* attr_type = json_object_new_string("string"); // Default type
                json_object_object_add(attr_schema, "type", attr_type);
                json_object_object_add(properties, resource->attributes[a], attr_schema);
            }
        }

        // Add relationships
        for (int r = 0; r < resource->relation_count && r < MAX_RELATIONS; r++) {
            if (strlen(resource->relations[r].name) > 0) {
                json_object* rel_schema = json_object_new_object();
                json_object* rel_type = json_object_new_string("object");
                json_object_object_add(rel_schema, "type", rel_type);
                json_object_object_add(properties, resource->relations[r].name, rel_schema);
            }
        }

        json_object_object_add(schema, "properties", properties);

        const char* schema_name = strlen(resource->model_name) > 0 ?
                                 resource->model_name : resource->class_name;
        json_object_object_add(schemas, schema_name, schema);
    }

    json_object_object_add(components, "schemas", schemas);
    json_object_object_add(root, "components", components);

    return root;
}

void print_usage(const char* program_name) {
    printf("Usage: %s <resource_directory> [routes_file]\n", program_name);
    printf("  resource_directory: Directory containing *_resource.rb files\n");
    printf("  routes_file: Optional path to config/routes.rb (default: config/routes.rb)\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* resource_dir = argv[1];
    const char* routes_file = (argc > 2) ? argv[2] : "config/routes.rb";

    ApiSpec spec;
    memset(&spec, 0, sizeof(ApiSpec));

    printf("Scanning resource files in: %s\n", resource_dir);
    scan_resource_files(resource_dir, &spec);

    printf("Parsing routes file: %s\n", routes_file);
    parse_routes_file(routes_file, &spec);

    printf("Generating JSON API specification...\n");
    json_object* json_spec = generate_json_api_spec(&spec);

    // Output the JSON
    const char* json_string = json_object_to_json_string_ext(json_spec, JSON_C_TO_STRING_PRETTY);
    if (json_string) {
        printf("%s\n", json_string);

        // Write to file
        FILE* output_file = fopen("api_spec.json", "w");
        if (output_file) {
            fprintf(output_file, "%s\n", json_string);
            fclose(output_file);
            printf("\nAPI specification written to api_spec.json\n");
        } else {
            printf("\nError: Could not write to api_spec.json\n");
        }
    } else {
        printf("Error: Could not generate JSON string\n");
    }

    json_object_put(json_spec);

    printf("\nParsed %d resources and %d routes\n", spec.resource_count, spec.route_count);

    return 0;
}

