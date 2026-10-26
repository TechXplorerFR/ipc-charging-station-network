# Exemple pour compiler un fichier de test
add_executable(test_allocator test_allocator.c)

# Le test a besoin de connaître les en-têtes
target_include_directories(test_allocator PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Déclarer le test pour qu'il soit exécuté avec la commande 'ctest'
add_test(NAME TestAllocator COMMAND test_allocator)