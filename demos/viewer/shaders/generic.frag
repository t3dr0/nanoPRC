#version 330 core

layout (location = 0) out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 Color;
in vec4 FragPosLightSpace;
in vec3 VDiffuseColor;
in vec3 VMaterialTint;
in vec3 VSpecularColor;
in vec3 VEmissiveColor;
in float VShininess;
in float VAlpha;

struct DirLight
{
    vec3 direction;
    vec3 color;
    float intensity;
};

struct PointLight
{
    vec3 position;
    vec3 color;
    float intensity;
};

struct Material
{
    vec3 diffuse;
    vec3 specular;
    float shininess;
    float alpha;
    vec3 tint;

    sampler2D diffuseMap;
    bool hasDiffuseMap;

    sampler2D specularMap;
    bool hasSpecularMap;
};

uniform vec3 uViewPos;

uniform DirLight uDirLight;
uniform PointLight uPointLight;

uniform vec3 uSkyColorHorizon;
uniform vec3 uSkyColorZenith;

uniform Material uMaterial;
uniform bool uIsLine;
uniform bool uVertexMaterial;

uniform sampler2D uShadowMap;
uniform bool uEnableShadows;

uniform bool uWireframe;
uniform bool uFullbright;

const float kShadowBias = 0.002;

/* Calculate visibility of fragment from light source */
float calcShadowFactor(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    if (!uEnableShadows)
        return 1.0;

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    /* Check if fragment is outside of light frustum */
    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x > 1.0 || projCoords.x < 0.0 || projCoords.y > 1.0 || projCoords.y < 0.0)
        return 1.0;
    
    float closestDepth = texture(uShadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;

    float bias = max(0.03 * (1.0 - dot(normal, lightDir)), kShadowBias);
    return currentDepth - bias > closestDepth ? 0.0 : 1.0;
}

/* Calculate lighting from a directional light (sun) */
vec3 calcDirLight(DirLight light, vec3 normal, vec3 diffuseContrib, vec3 specularContrib)
{
    if (light.intensity <= 0.0)
        return vec3(0.0);

    vec3 L = normalize(-light.direction);
    vec3 V = normalize(uViewPos - FragPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(normal, L), 0.0);
    if (diff <= 0.0)
        return vec3(0.0);

    float shadowFactor = calcShadowFactor(FragPosLightSpace, normal, L);
    if (shadowFactor <= 0.0)
        return vec3(0.0);

    vec3 diffuse = vec3(diff);// * diffuseContrib;

    vec3 specular = vec3(0.0);
    if (diff > 0.0)
    {
        vec3 R = 2 * dot(normal, L) * normal - L;
        specular = specularContrib * pow(max(dot(R, V), 0.0), uMaterial.shininess);
        //float spec = pow(max(dot(normal, H), 0.0), uMaterial.shininess);
        //specular = spec * specularContrib;
    }

    return (diffuse + specular) * light.color * light.intensity * shadowFactor;
}

/* Calculate lighting from a point light */
vec3 calcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 diffuseContrib, vec3 specularContrib)
{
    if (light.intensity <= 0.0)
        return vec3(0.0);

    vec3 L = normalize(light.position - fragPos);
    vec3 V = normalize(uViewPos - fragPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(normal, L), 0.0);
    vec3 diffuse = vec3(diff);// * diffuseContrib;

    vec3 specular = vec3(0.0);
    if (diff > 0.0)
    {
        float spec = pow(max(dot(normal, H), 0.0), uMaterial.shininess);
        specular = spec * specularContrib;
    }

    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / distance;

    return (diffuse + specular) * light.color * attenuation * light.intensity;
}

void main()
{
    // DEBUG: Test if back-facing fragments are even executing
   // FragColor = gl_FrontFacing ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 0.0, 1.0, 1.0);
   // return;

    if (uWireframe)
    {
        FragColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }
	
	if (uIsLine)
	{
        // For markups (lines and text) we want an unlit path
        if (uMaterial.hasDiffuseMap)
        {
            // TEXT RENDERING: Font atlas glyph coverage in ALPHA channel
            vec4 tex = texture(uMaterial.diffuseMap, TexCoords);
            
            // Font atlas: RGB=white (ignored), Alpha=glyph coverage (0-1)
            float glyphCoverage = tex.a;
            
            // Vertex color contains the desired text color
            vec3 textColor = Color * uMaterial.tint;
            
            // Use glyph coverage as the alpha for the text color
            FragColor = vec4(textColor, glyphCoverage * uMaterial.alpha);
           // FragColor = vec4(textColor, glyphCoverage);
        }
        else
        {
            // LINE RENDERING: Plain color (no texture)
            FragColor = vec4(Color * uMaterial.tint, uMaterial.alpha);
        }
		return;
	}

    if (uFullbright)
    {
        vec3 color = uMaterial.hasDiffuseMap ? texture(uMaterial.diffuseMap, TexCoords).rgb : uMaterial.diffuse;

        color *= uMaterial.tint * Color;

        FragColor = vec4(color, 1.0);
		//FragColor = vec4(TexCoords, 0.0, 1.0); /* For displaying texture coordinates */
        return;
    }

    if (uVertexMaterial)
    {
        /* Compute ambient color */
        float cosTheta = max(dot(Normal, vec3(0.0, 1.0, 0.0)), 0.0);
        vec3 ambient = mix(uSkyColorHorizon, uSkyColorZenith, cosTheta);

        vec3 diffuseContrib = VDiffuseColor;
        vec3 specularContrib = VSpecularColor;
        
        /* Adjust normal if back face */
        vec3 normal = gl_FrontFacing ? Normal : -Normal;

        /* Compute lighting */
        vec3 lighting = ambient; // Ambient
        lighting += calcDirLight(uDirLight, normal, diffuseContrib, specularContrib); // Directional light
        lighting += calcPointLight(uPointLight, normal, FragPos, diffuseContrib, specularContrib); // Point light

        /* Final color  Tint is ambient */
        vec3 color = 0.1 * VMaterialTint + lighting * Color * diffuseContrib;
        // FragColor = vec4(normal * 0.5 + 0.5, 1.0);  /* Debug of normal vectors */

        FragColor = vec4(color, 1.0);
        return;
    }

    /* Compute ambient color */
    float cosTheta = max(dot(Normal, vec3(0.0, 1.0, 0.0)), 0.0);
    vec3 ambient = mix(uSkyColorHorizon, uSkyColorZenith, cosTheta) * 0.5; // Add a multiplier to brighten;

    /* Sample textures */
    vec3 diffuseContrib = uMaterial.hasDiffuseMap ? texture(uMaterial.diffuseMap, TexCoords).rgb : uMaterial.diffuse;
    vec3 specularContrib = uMaterial.hasSpecularMap ? texture(uMaterial.specularMap, TexCoords).rgb : uMaterial.specular;
    float alphaDiffuse = uMaterial.hasDiffuseMap ? texture(uMaterial.diffuseMap, TexCoords).a : 1.0;
    float alphaSpecular = uMaterial.hasSpecularMap ? texture(uMaterial.specularMap, TexCoords).a : 1.0;
    float alpha = min(alphaDiffuse, alphaSpecular);
    
    /* Adjust normal if back face */
    vec3 normal = gl_FrontFacing ? Normal : -Normal;

    /* Compute lighting */
    vec3 lighting = ambient; // Ambient
    lighting += calcDirLight(uDirLight, normal, diffuseContrib, specularContrib); // Directional light
    lighting += calcPointLight(uPointLight, normal, FragPos, diffuseContrib, specularContrib); // Point light

    /* Final color.  Tint is ambient */
    vec3 color = 0.1 * uMaterial.tint + lighting * Color * diffuseContrib;


	// TEMPORARY DEBUG - replace with this line:
   // color = gl_FrontFacing ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
	// DEBUG: Visualize alpha values
  //  color = vec3(uMaterial.alpha, alphaDiffuse, alpha);
  

    // FragColor = vec4(normal * 0.5 + 0.5, 1.0);  /* Debug of normal vectors */

    //FragColor = vec4(alphaDiffuse, alphaSpecular, 0, 1.0);
   // FragColor = vec4(color, uMaterial.alpha);

       // DEBUG: Visualize alpha values - REMOVE AFTER TESTING
    //color = vec3(alpha, uMaterial.alpha, alpha * uMaterial.alpha);

   FragColor = vec4(color, alpha * uMaterial.alpha);

    return;
}
