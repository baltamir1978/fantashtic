#pragma once
//
// Copiar a secrets.h y rellenar. secrets.h ESTA EN .gitignore y no debe
// commitearse nunca.
//
// El archivo es OPCIONAL: config.h lo incluye con __has_include, asi que un
// clon recien hecho compila y pasa los 63 tests sin crearlo.
//
// La frase del grupo NO se pone aqui. Se teclea en el asistente de primer
// arranque y la clave derivada se guarda en NVS del aparato.
//

// -----------------------------------------------------------------------------
// Sal de PBKDF2 de tu despliegue.
//
// Sin definir nada se usa la de config.h ("fantashtic-v1"), que es publica y la
// misma para todo el que clone este repositorio. La sal nunca fue el secreto
// -- la frase si -- pero poner una propia hace que una tabla precalculada contra
// Fantashtic no sirva contra TU grupo: hay que rehacer las 20 000 iteraciones de
// PBKDF2 por cada sal. Es lo que se suele llamar pepper.
//
// Generala al azar, no la elijas a mano. En PowerShell:
//   $b = New-Object byte[] 18
//   [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b)
//   [Convert]::ToBase64String($b) -replace '[+/=]',''
//
// AVISO: la clave derivada vive en NVS, asi que cambiar la sal no rompe los
// nodos ya configurados. Pero un nodo NUEVO derivara otra clave de la MISMA
// frase y no se entendera con ellos. Al poner o cambiar la sal, repasa la frase
// del grupo en TODOS los nodos de una vez.
//
// #define PBKDF2_SALT "fantashtic-pon-aqui-lo-tuyo"
// -----------------------------------------------------------------------------

// Nombres por defecto de este nodo. Sin usar todavia: hoy los nombres salen del
// asistente de primer arranque.
#define FT_DEFAULT_LONGNAME    "Nodo sin nombre"
#define FT_DEFAULT_SHORTNAME   "NODO"
