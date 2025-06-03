
### Description

This tool help to convert your JSON API resource files taken from rails project to OPEN API spec file

how to compile in MacOS
```bash

gcc -Wall -I/opt/homebrew/include jsonapi-resources_parser.c -o jsonapi-resources_parser -L/opt/homebrew/lib -ljson-c
```

how to use 

```bash

./rails_parser app/resources/api/rest/customer/v1/ config/routes.rb
```
