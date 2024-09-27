// minimalistic code to draw a single triangle, this is not part of the API.
#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#ifdef _WIN32				 // must use MT platform DLL libraries on windows
#pragma comment(lib, "shaderc_combined.lib")
#endif
#include "Camera.h"
#include "TextureUtils.h"
void PrintLabeledDebugString(const char *label, const char *toPrint)
{
	std::cout << label << toPrint << std::endl;

	// OutputDebugStringA is a windows-only function
#if defined WIN32
	OutputDebugStringA(label);
	OutputDebugStringA(toPrint);
#endif
}

class Renderer
{
	static const uint32_t MAX_TEXTURES = 16; // Or any other reasonable maximum

	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	VkRenderPass renderPass;
	GW::CORE::GEventReceiver shutdown;

	VkDevice device = nullptr;
	VkPhysicalDevice physicalDevice = nullptr;

	VkBuffer unifiedBufferHandle = nullptr;
	VkDeviceMemory unifiedBufferData = nullptr;
	VkShaderModule vertexShader = nullptr;
	VkShaderModule fragmentShader = nullptr;
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;

	unsigned int windowWidth, windowHeight;

	VkBuffer geometryBuffer = nullptr;
	VkDeviceMemory geometryBufferMemory = nullptr;
	VkDeviceSize vertexBufferOffset = 0;
	VkDeviceSize indexBufferOffset = 0;
	uint32_t indexCount = 0;
	VkIndexType indexType = VK_INDEX_TYPE_UINT16;
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;

	std::vector<VkDeviceSize> attributeOffsets;
	std::vector<VkDeviceSize> attributeSizes;
	VkDeviceSize totalBufferSize;

	// D3

	struct SHADER_VARS
	{	
		GW::MATH::GMATRIXF viewMatrix;
		GW::MATH::GMATRIXF projectionMatrix;
		GW::MATH::GVECTORF sunDirection;
		GW::MATH::GVECTORF sunColor;
		GW::MATH::GVECTORF cameraPosition;
		GW::MATH::GMATRIXF worldMatrix;
		//GW::MATH::GMATRIXF worldMatrix;
		// float padding[12]; // Adjust padding as needed
	};
	GW::MATH::GMatrix math;
	GW::MATH::GMATRIXF viewMatrix;
	GW::MATH::GMATRIXF projectionMatrix;
	std::vector<VkBuffer> uniformBuffers;
	std::vector<VkDeviceMemory> uniformBuffersMemory;

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;
	std::vector<VkDescriptorSet> descriptorSets;

	// WORK 2

	VkBuffer textureBuffer;
	VkDeviceMemory textureMemory;
	VkImage textureImage;
	VkImageView textureImageView;
	VkSampler textureSampler;

	VkDescriptorSetLayout textureDescriptorSetLayout;
	std::vector<VkDescriptorSet> textureDescriptorSets;

	// part c
	struct TextureData
	{
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkImage image;
		VkImageView imageView;
		VkSampler sampler;
	};
	std::vector<TextureData> textures;

	// Work 3
	std::chrono::steady_clock::time_point startTime; 


public:
	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk)
	{
		startTime = std::chrono::steady_clock::now();
		win = _win;
		vlk = _vlk;
		math.Create();
		LoadGLTFModel("../Models/WaterBottle/glTF/blender_waterbottle.gltf");



		CreateViewMatrix();
		CreateProjectionMatrix();

		UpdateWindowDimensions();
		InitializeGraphics();
		BindShutdownCallback();
	}

private:
	void CreateDescriptorSetLayout()
	{
		VkDescriptorSetLayoutBinding layoutBinding{};
		layoutBinding.binding = 0;
		layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layoutBinding.descriptorCount = 1;
		layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &layoutBinding;

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create descriptor set layout!");
		}
	}

	void CreateDescriptorSet()
	{
		unsigned int imageCount;
		vlk.GetSwapchainImageCount(imageCount);
		descriptorSets.resize(imageCount);

		std::vector<VkDescriptorSetLayout> layouts(imageCount, descriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = imageCount;
		allocInfo.pSetLayouts = layouts.data();

		if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate descriptor sets!");
		}
	}
	void CreateDescriptorPool()
	{
		unsigned int imageCount;
		vlk.GetSwapchainImageCount(imageCount);

		std::array<VkDescriptorPoolSize, 2> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = static_cast<uint32_t>(imageCount);
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = static_cast<uint32_t>(imageCount * MAX_TEXTURES);

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = static_cast<uint32_t>(imageCount) * 2;

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create descriptor pool!");
		}
	}

	void UpdateDescriptorSet()
	{
		unsigned int imageCount;
		vlk.GetSwapchainImageCount(imageCount);
		for (size_t i = 0; i < imageCount; i++)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformBuffers[i];
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(SHADER_VARS);

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSets[i];
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pBufferInfo = &bufferInfo;

			vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
		}
	}

	void CreateTextureDescriptorSetLayout()
	{
		VkDescriptorSetLayoutBinding samplerLayoutBinding{};
		samplerLayoutBinding.binding = 0;
		samplerLayoutBinding.descriptorCount = MAX_TEXTURES;
		samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLayoutBinding.pImmutableSamplers = nullptr;
		samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlags{};
		bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
		bindingFlags.bindingCount = 1;
		VkDescriptorBindingFlagsEXT flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;
		bindingFlags.pBindingFlags = &flags;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &samplerLayoutBinding;
		layoutInfo.pNext = &bindingFlags;

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &textureDescriptorSetLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create texture descriptor set layout!");
		}
	}
	void CreateTextureDescriptorSet()
	{
		unsigned int imageCount;
		vlk.GetSwapchainImageCount(imageCount);

		std::vector<VkDescriptorSetLayout> layouts(imageCount, textureDescriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = imageCount;
		allocInfo.pSetLayouts = layouts.data();

		VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variableCountInfo{};
		variableCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
		std::vector<uint32_t> descriptorCounts(imageCount, static_cast<uint32_t>(textures.size()));
		variableCountInfo.descriptorSetCount = imageCount;
		variableCountInfo.pDescriptorCounts = descriptorCounts.data();

		allocInfo.pNext = &variableCountInfo;

		textureDescriptorSets.resize(imageCount);
		if (vkAllocateDescriptorSets(device, &allocInfo, textureDescriptorSets.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate texture descriptor sets!");
		}

		for (unsigned int i = 0; i < imageCount; i++)
		{
			std::vector<VkDescriptorImageInfo> imageInfos(textures.size());
			for (size_t j = 0; j < textures.size(); j++)
			{
				imageInfos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageInfos[j].imageView = textures[j].imageView;
				imageInfos[j].sampler = textures[j].sampler;
			}

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = textureDescriptorSets[i];
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = static_cast<uint32_t>(textures.size());
			descriptorWrite.pImageInfo = imageInfos.data();

			vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
		}
	}
	void BindTextureDescriptorSet(VkCommandBuffer &commandBuffer, uint32_t imageIndex)
	{
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &textureDescriptorSets[imageIndex], 0, nullptr);
	}

	void CreateUniformBuffers()
	{
		VkDeviceSize bufferSize = sizeof(SHADER_VARS);

		unsigned int imageCount;
		if (vlk.GetSwapchainImageCount(imageCount) != GW::GReturn::SUCCESS)
		{
			throw std::runtime_error("Failed to get swapchain image count");
		}

		uniformBuffers.resize(imageCount);
		uniformBuffersMemory.resize(imageCount);

		for (size_t i = 0; i < imageCount; i++)
		{
			if (GvkHelper::create_buffer(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
										 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
										 &uniformBuffers[i], &uniformBuffersMemory[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create uniform buffer");
			}
		}
	}
	void CreateViewMatrix()
	{
		GW::MATH::GVECTORF eyePosition = {0.5f, 0.0f, -0.3f, 1.0f};
		GW::MATH::GVECTORF lookAtPoint = {0.0f, 0.0f, 0.0f, 1.0f};
		GW::MATH::GVECTORF upDirection = {0.0f, 1.0f, 0.0f, 0.0f};

		if (math.LookAtLHF(eyePosition, lookAtPoint, upDirection, viewMatrix) != GW::GReturn::SUCCESS)
		{
			throw std::runtime_error("Failed to create view matrix");
		}
	}

#include <cmath>
#include <corecrt_math_defines.h>
#define _USE_MATH_DEFINES
	void CreateProjectionMatrix()
	{
		GW::MATH::GMATRIXF projectionMatrix;
		float fov = 40.0f * (M_PI / 180.0f); //  degrees to radians
		float farPlane = 0.1f;
		float nearPlane = 100.0f;

		float aspectRatio;
		vlk.GetAspectRatio(aspectRatio);

		// DirectX-style perspective projection matrix
		if (math.ProjectionDirectXLHF(fov, aspectRatio, nearPlane, farPlane, projectionMatrix) != GW::GReturn::SUCCESS)
		{
			throw std::runtime_error("Failed to create DirectX-style projection matrix");
		}

		this->projectionMatrix = projectionMatrix;
	}
	void ApplyZAxisFlip(GW::MATH::GMATRIXF& matrix)
	{
		// Define the scaling vector to flip the Z-axis
		GW::MATH::GVECTORF scalingVector = { 1.0f, 1.0f, -1.0f, 0.0f }; // No change to W

		// Create an output matrix to store the result
		GW::MATH::GMATRIXF outMatrix;

		// Apply the scaling to flip the Z-axis
		if (GW::MATH::GMatrix::ScaleLocalF(matrix, scalingVector, outMatrix) == GW::GReturn::SUCCESS)
		{
			matrix = outMatrix;  // Store the flipped matrix
		}
	}

	void UpdateUniformBuffer(uint32_t currentImage)
	{
		SHADER_VARS shaderVars;
		GW::MATH::GMATRIXF worldMatrix; 
		math.IdentityF(worldMatrix); // Set the world matrix to identity (no transformation
		worldMatrix.row3 = { 0.0f, 0.0f, -1.0f, 0.0f }; // Set the translation component of the world matrix
		// memcpy(shaderVars.worldMatrix, worldMatrix, sizeof(worldMatrix));
		shaderVars.worldMatrix = worldMatrix;
		shaderVars.viewMatrix = viewMatrix;
		shaderVars.projectionMatrix = projectionMatrix;

		auto currentTime = std::chrono::steady_clock::now();
		float elapsedSeconds = std::chrono::duration<float>(currentTime - startTime).count();

		float dayDuration = 0.01f; // Full day in hours
		float timeFactor = (elapsedSeconds / (dayDuration * 3600.0f)) * 2.0f * 3.14159f; 

		float sunHeight = -1.0f; // (0 = horizon, 1 = zenith)
		float sunAngle = timeFactor; 
		shaderVars.sunDirection = {
		cos(sunAngle),
		sin(sunHeight * 3.14159f / 2.0f), // Adjust height based on desired vertical movement
		sin(sunAngle),
		0.0f
		};


		shaderVars.sunColor = {1.0f, 1.0f, 1.0f, 1.0f};			// White light
		shaderVars.cameraPosition = {-0.5f, 0.25f, 0.5f, 1.0f}; // camera at origin
		
		

		void *data;
		vkMapMemory(device, uniformBuffersMemory[currentImage], 0, sizeof(shaderVars), 0, &data);

		memcpy(data, &shaderVars, sizeof(shaderVars));

		vkUnmapMemory(device, uniformBuffersMemory[currentImage]);

		GvkHelper::write_to_buffer(device, uniformBuffersMemory[currentImage], &shaderVars, sizeof(shaderVars));
	}

	void LoadGLTFModel(const std::string &filepath)
	{
		std::string err;
		std::string warn;

		bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filepath);

		if (!warn.empty())
		{
			std::cout << "GLTF Warning: " << warn << std::endl;
		}

		if (!err.empty())
		{
			std::cout << "GLTF Error: " << err << std::endl;
		}

		if (!ret)
		{
			throw std::runtime_error("Failed to load GLTF model");
		}

		std::cout << "Loaded GLTF model with " << model.meshes.size() << " meshes" << std::endl;

		const tinygltf::Mesh &mesh = model.meshes[0];
		const tinygltf::Primitive &primitive = mesh.primitives[0]; // Assuming a single primitive

		const tinygltf::Accessor &indexAccessor = model.accessors[primitive.indices];
		const tinygltf::BufferView &indexBufferView = model.bufferViews[indexAccessor.bufferView];
		indexBufferOffset = indexBufferView.byteOffset;

		int positionAccessorIndex = primitive.attributes.find("POSITION")->second;
		const tinygltf::Accessor &positionAccessor = model.accessors[positionAccessorIndex];
		const tinygltf::BufferView &vertexBufferView = model.bufferViews[positionAccessor.bufferView];
		vertexBufferOffset = vertexBufferView.byteOffset;
		indexCount = indexAccessor.count;

		// Texture loading
		if (!model.materials.empty())
		{
			const tinygltf::Material &material = model.materials[0];

			auto loadTextureFromMaterial = [&](const std::string &textureName)
			{
				auto it = material.values.find(textureName);
				if (it != material.values.end())
				{
					int textureIndex = it->second.TextureIndex();
					if (textureIndex >= 0 && textureIndex < model.textures.size())
					{
						const tinygltf::Texture &texture = model.textures[textureIndex];
						int imageIndex = texture.source;
						if (imageIndex >= 0 && imageIndex < model.images.size())
						{
							const tinygltf::Image &image = model.images[imageIndex];

							TextureData textureData;
							UploadTextureToGPU(vlk, image, textureData.buffer, textureData.memory, textureData.image, textureData.imageView);
							VkResult result = CreateSampler(vlk, textureData.sampler);
							if (result != VK_SUCCESS)
							{
								throw std::runtime_error("Failed to create texture sampler!");
							}
							textures.push_back(textureData);
						}
					}
				}
			};

			// Load baseColor texture
			loadTextureFromMaterial("baseColorTexture");

			loadTextureFromMaterial("metallicRoughnessTexture");
		}
	}

	void UpdateWindowDimensions()
	{
		win.GetClientWidth(windowWidth);
		win.GetClientHeight(windowHeight);
	}

	void InitializeGraphics()
	{
		GetHandlesFromSurface();
		InitializeVertexBuffer();

		CreateUniformBuffers();

		CreateDescriptorSetLayout();
		CreateTextureDescriptorSetLayout();
		CreateDescriptorPool();
		CreateDescriptorSet();
		CreateTextureDescriptorSet();
		UpdateDescriptorSet();

		CompileShaders();
		InitializeGraphicsPipeline();
	}

	void GetHandlesFromSurface()
	{
		vlk.GetDevice((void **)&device);
		vlk.GetPhysicalDevice((void **)&physicalDevice);
		vlk.GetRenderPass((void **)&renderPass);
	}

	void InitializeVertexBuffer()
	{
		float verts[] =
			{
				0, 0.5f,
				0.5f, -0.5f,
				-0.5f, -0.5f};

		CreateUnifiedBuffer();
	}

	void CreateUnifiedBuffer()
	{
		const tinygltf::Mesh &mesh = model.meshes[0];
		const tinygltf::Primitive &primitive = mesh.primitives[0];

		attributeOffsets.resize(4);
		attributeSizes.resize(4);
		totalBufferSize = 0;

		// Calculate sizes and offsets for each attribute
		for (int i = 0; i < 4; ++i)
		{
			const tinygltf::Accessor &accessor = model.accessors[i];
			const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
			attributeSizes[i] = bufferView.byteLength;
			attributeOffsets[i] = totalBufferSize;
			totalBufferSize += attributeSizes[i];
		}

		// Add index buffer size
		const tinygltf::Accessor &indexAccessor = model.accessors[primitive.indices];
		const tinygltf::BufferView &indexBufferView = model.bufferViews[indexAccessor.bufferView];
		VkDeviceSize indexBufferSize = indexBufferView.byteLength;
		indexBufferOffset = totalBufferSize;
		totalBufferSize += indexBufferSize;

		// Create unified buffer
		GvkHelper::create_buffer(physicalDevice, device, totalBufferSize,
								 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
								 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
								 &unifiedBufferHandle, &unifiedBufferData);

		// Copy vertex data
		for (int i = 0; i < 4; ++i)
		{
			const tinygltf::Accessor &accessor = model.accessors[i];
			const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
			const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

			void *data;
			vkMapMemory(device, unifiedBufferData, attributeOffsets[i], attributeSizes[i], 0, &data);
			memcpy(data, &buffer.data[bufferView.byteOffset], attributeSizes[i]);
			vkUnmapMemory(device, unifiedBufferData);
		}

		// Copy index data
		void *data;
		vkMapMemory(device, unifiedBufferData, indexBufferOffset, indexBufferSize, 0, &data);
		memcpy(data, &model.buffers[indexBufferView.buffer].data[indexBufferView.byteOffset], indexBufferSize);
		vkUnmapMemory(device, unifiedBufferData);

		indexCount = indexAccessor.count;

		std::cout << "Total buffer size: " << totalBufferSize << std::endl;
		for (int i = 0; i < 4; ++i)
		{
			std::cout << "Attribute " << i << " - Offset: " << attributeOffsets[i]
					  << ", Size: " << attributeSizes[i] << std::endl;
		}
		std::cout << "Index buffer - Offset: " << indexBufferOffset
				  << ", Size: " << indexBufferSize << std::endl;
	}
	// void CreateUnifiedBuffer()
	//{
	//	const tinygltf::Mesh& mesh = model.meshes[0];
	//	const tinygltf::Primitive& primitive = mesh.primitives[0];

	//	const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
	//	const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
	//	const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

	//	int positionAccessorIndex = primitive.attributes.find("POSITION")->second;
	//	const tinygltf::Accessor& positionAccessor = model.accessors[positionAccessorIndex];
	//	const tinygltf::BufferView& positionBufferView = model.bufferViews[positionAccessor.bufferView];
	//	const tinygltf::Buffer& positionBuffer = model.buffers[positionBufferView.buffer];

	//	VkDeviceSize indexBufferSize = indexAccessor.count * sizeof(uint16_t);
	//	VkDeviceSize vertexBufferSize = positionAccessor.count * 3 * sizeof(float);

	//	// Ensure index buffer size is aligned to 4 bytes
	//	VkDeviceSize alignedIndexBufferSize = (indexBufferSize + 3) & ~3;

	//	VkDeviceSize totalBufferSize = alignedIndexBufferSize + vertexBufferSize;

	//	// Create unified buffer
	//	GvkHelper::create_buffer(physicalDevice, device, totalBufferSize,
	//		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	//		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	//		&unifiedBufferHandle, &unifiedBufferData);

	//	// Copy index data
	//	void* data;
	//	vkMapMemory(device, unifiedBufferData, 0, alignedIndexBufferSize, 0, &data);
	//	memcpy(data, &indexBuffer.data[indexBufferView.byteOffset], indexBufferSize);
	//	vkUnmapMemory(device, unifiedBufferData);

	//	// Copy vertex data
	//	vkMapMemory(device, unifiedBufferData, alignedIndexBufferSize, vertexBufferSize, 0, &data);
	//	memcpy(data, &positionBuffer.data[positionBufferView.byteOffset], vertexBufferSize);
	//	vkUnmapMemory(device, unifiedBufferData);

	//	indexBufferOffset = 0;
	//	vertexBufferOffset = alignedIndexBufferSize;
	//	indexCount = indexAccessor.count;
	//}

	void CompileShaders()
	{
		// Intialize runtime shader compiler HLSL -> SPIRV
		shaderc_compiler_t compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t options = CreateCompileOptions();

		CompileVertexShader(compiler, options);
		CompileFragmentShader(compiler, options);

		// Free runtime shader compiler resources
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);
	}

	shaderc_compile_options_t CreateCompileOptions()
	{
		shaderc_compile_options_t retval = shaderc_compile_options_initialize();
		shaderc_compile_options_set_source_language(retval, shaderc_source_language_hlsl);
		shaderc_compile_options_set_invert_y(retval, true);
#ifndef NDEBUG
		shaderc_compile_options_set_generate_debug_info(retval);
#endif
		return retval;
	}

	void CompileVertexShader(const shaderc_compiler_t &compiler, const shaderc_compile_options_t &options)
	{
		std::string vertexShaderSource = ReadFileIntoString("../VertexShader.hlsl");

		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, vertexShaderSource.c_str(), vertexShaderSource.length(),
			shaderc_vertex_shader, "main.vert", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Vertex Shader Errors: \n", shaderc_result_get_error_message(result));
			abort(); // Vertex shader failed to compile!
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
										(char *)shaderc_result_get_bytes(result), &vertexShader);

		shaderc_result_release(result); // done
	}

	void CompileFragmentShader(const shaderc_compiler_t &compiler, const shaderc_compile_options_t &options)
	{
		std::string fragmentShaderSource = ReadFileIntoString("../FragmentShader.hlsl");

		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, fragmentShaderSource.c_str(), fragmentShaderSource.length(),
			shaderc_fragment_shader, "main.frag", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Fragment Shader Errors: \n", shaderc_result_get_error_message(result));
			abort(); // Fragment shader failed to compile!
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
										(char *)shaderc_result_get_bytes(result), &fragmentShader);

		shaderc_result_release(result); // done
	}

	std::vector<VkVertexInputAttributeDescription> CreateVkVertexInputAttributeDescriptions()
	{
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions(4);

		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = 0;

		attributeDescriptions[1].binding = 1;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = 0;

		attributeDescriptions[2].binding = 2;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[2].offset = 0;

		attributeDescriptions[3].binding = 3;
		attributeDescriptions[3].location = 3;
		attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[3].offset = 0;

		return attributeDescriptions;
	}

	std::vector<VkVertexInputBindingDescription> CreateVkVertexInputBindingDescriptions()
	{
		std::vector<VkVertexInputBindingDescription> bindingDescriptions(4);

		for (int i = 0; i < 4; ++i)
		{
			bindingDescriptions[i].binding = i;
			bindingDescriptions[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		}

		// Set strides based on the accessor's byte stride
		bindingDescriptions[0].stride = model.accessors[0].ByteStride(model.bufferViews[model.accessors[0].bufferView]);
		bindingDescriptions[1].stride = model.accessors[1].ByteStride(model.bufferViews[model.accessors[1].bufferView]);
		bindingDescriptions[2].stride = model.accessors[2].ByteStride(model.bufferViews[model.accessors[2].bufferView]);
		bindingDescriptions[3].stride = model.accessors[3].ByteStride(model.bufferViews[model.accessors[3].bufferView]);

		return bindingDescriptions;
	}
	// Create Pipeline & Layout (Thanks Tiny!)
	void InitializeGraphicsPipeline()
	{
		VkPipelineShaderStageCreateInfo stage_create_info[2] = {};

		// Create Stage Info for Vertex Shader
		stage_create_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage_create_info[0].module = vertexShader;
		stage_create_info[0].pName = "main";

		// Create Stage Info for Fragment Shader
		stage_create_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stage_create_info[1].module = fragmentShader;
		stage_create_info[1].pName = "main";

		VkPipelineInputAssemblyStateCreateInfo assembly_create_info = CreateVkPipelineInputAssemblyStateCreateInfo();
		auto vertex_binding_description = CreateVkVertexInputBindingDescriptions();

		// vertex_binding_description.binding = 0;
		// vertex_binding_description.stride = 3 * sizeof(float); // 3 floats for position (12 bytes)
		// vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		auto attributeDescriptions = CreateVkVertexInputAttributeDescriptions();

		/*VkVertexInputAttributeDescription vertex_attribute_descriptions[1];
		vertex_attribute_descriptions[0].binding = 0;
		vertex_attribute_descriptions[0].location = 0;
		vertex_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertex_attribute_descriptions[0].offset = 0;*/
		VkPipelineVertexInputStateCreateInfo input_vertex_info = CreateVkPipelineVertexInputStateCreateInfo(
			vertex_binding_description.data(), vertex_binding_description.size(),
			attributeDescriptions.data(), attributeDescriptions.size());
		VkViewport viewport = CreateViewportFromWindowDimensions();
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		VkPipelineViewportStateCreateInfo viewport_create_info = CreateVkPipelineViewportStateCreateInfo(&viewport, 1, &scissor, 1);
		VkPipelineRasterizationStateCreateInfo rasterization_create_info = CreateVkPipelineRasterizationStateCreateInfo();
		VkPipelineMultisampleStateCreateInfo multisample_create_info = CreateVkPipelineMultisampleStateCreateInfo();
		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = CreateVkPipelineDepthStencilStateCreateInfo();
		VkPipelineColorBlendAttachmentState color_blend_attachment_state = CreateVkPipelineColorBlendAttachmentState();
		VkPipelineColorBlendStateCreateInfo color_blend_create_info = CreateVkPipelineColorBlendStateCreateInfo(&color_blend_attachment_state, 1);

		VkDynamicState dynamic_states[2] =
			{
				// By setting these we do not need to re-create the pipeline on Resize
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR};

		VkPipelineDynamicStateCreateInfo dynamic_create_info = CreateVkPipelineDynamicStateCreateInfo(dynamic_states, 2);

		CreatePipelineLayout();

		// Pipeline State... (FINALLY)
		VkGraphicsPipelineCreateInfo pipeline_create_info = {};
		pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = stage_create_info;
		pipeline_create_info.pInputAssemblyState = &assembly_create_info;
		pipeline_create_info.pVertexInputState = &input_vertex_info;
		pipeline_create_info.pViewportState = &viewport_create_info;
		pipeline_create_info.pRasterizationState = &rasterization_create_info;
		pipeline_create_info.pMultisampleState = &multisample_create_info;
		pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
		pipeline_create_info.pColorBlendState = &color_blend_create_info;
		pipeline_create_info.pDynamicState = &dynamic_create_info;
		pipeline_create_info.layout = pipelineLayout;
		pipeline_create_info.renderPass = renderPass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;

		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline);
	}

	VkPipelineShaderStageCreateInfo CreateVertexShaderStageCreateInfo()
	{
		VkPipelineShaderStageCreateInfo retval;
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		retval.stage = VK_SHADER_STAGE_VERTEX_BIT;
		retval.module = vertexShader;
		retval.pName = "main";
		return retval;
	}

	VkPipelineInputAssemblyStateCreateInfo CreateVkPipelineInputAssemblyStateCreateInfo()
	{
		VkPipelineInputAssemblyStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		retval.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		retval.primitiveRestartEnable = false;
		return retval;
	}

	VkPipelineVertexInputStateCreateInfo CreateVkPipelineVertexInputStateCreateInfo(
		VkVertexInputBindingDescription *inputBindingDescriptions, unsigned int bindingCount,
		VkVertexInputAttributeDescription *vertexAttributeDescriptions, unsigned int attributeCount)
	{
		VkPipelineVertexInputStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		retval.vertexBindingDescriptionCount = bindingCount;
		retval.pVertexBindingDescriptions = inputBindingDescriptions;
		retval.vertexAttributeDescriptionCount = attributeCount;
		retval.pVertexAttributeDescriptions = vertexAttributeDescriptions;
		return retval;
	}

	VkViewport CreateViewportFromWindowDimensions()
	{
		VkViewport retval = {};
		retval.x = 0;
		retval.y = 0;
		retval.width = static_cast<float>(windowWidth);
		retval.height = static_cast<float>(windowHeight);
		retval.minDepth = 1;
		retval.maxDepth = 0;
		return retval;
	}

	VkRect2D CreateScissorFromWindowDimensions()
	{
		VkRect2D retval = {};
		retval.offset.x = 0;
		retval.offset.y = 0;
		retval.extent.width = windowWidth;
		retval.extent.height = windowHeight;
		return retval;
	}

	VkPipelineViewportStateCreateInfo CreateVkPipelineViewportStateCreateInfo(VkViewport *viewports, unsigned int viewportCount, VkRect2D *scissors, unsigned int scissorCount)
	{
		VkPipelineViewportStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		retval.viewportCount = viewportCount;
		retval.pViewports = viewports;
		retval.scissorCount = scissorCount;
		retval.pScissors = scissors;
		return retval;
	}

	VkPipelineRasterizationStateCreateInfo CreateVkPipelineRasterizationStateCreateInfo()
	{
		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f;
		rasterizer.depthBiasClamp = 0.0f;
		rasterizer.depthBiasSlopeFactor = 0.0f;
		return rasterizer;
	}

	VkPipelineMultisampleStateCreateInfo CreateVkPipelineMultisampleStateCreateInfo()
	{
		VkPipelineMultisampleStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		retval.sampleShadingEnable = VK_FALSE;
		retval.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		retval.minSampleShading = 1.0f;
		retval.pSampleMask = VK_NULL_HANDLE;
		retval.alphaToCoverageEnable = VK_FALSE;
		retval.alphaToOneEnable = VK_FALSE;
		return retval;
	}

	VkPipelineDepthStencilStateCreateInfo CreateVkPipelineDepthStencilStateCreateInfo()
	{
		VkPipelineDepthStencilStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		retval.depthTestEnable = VK_TRUE;
		retval.depthWriteEnable = VK_TRUE;
		retval.depthCompareOp = VK_COMPARE_OP_LESS;
		retval.depthBoundsTestEnable = VK_FALSE;
		retval.maxDepthBounds = 0.0f;
		retval.minDepthBounds = 1.0f;
		retval.stencilTestEnable = VK_FALSE;
		return retval;
	}

	VkPipelineColorBlendAttachmentState CreateVkPipelineColorBlendAttachmentState()
	{
		VkPipelineColorBlendAttachmentState retval = {};
		retval.colorWriteMask = 0xF;
		retval.blendEnable = VK_FALSE;
		retval.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		retval.dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		retval.colorBlendOp = VK_BLEND_OP_ADD;
		retval.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		retval.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		retval.alphaBlendOp = VK_BLEND_OP_ADD;
		return retval;
	}

	VkPipelineColorBlendStateCreateInfo CreateVkPipelineColorBlendStateCreateInfo(VkPipelineColorBlendAttachmentState *attachments, unsigned int attachmentCount)
	{
		VkPipelineColorBlendStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		retval.logicOpEnable = VK_FALSE;
		retval.logicOp = VK_LOGIC_OP_COPY;
		retval.attachmentCount = attachmentCount;
		retval.pAttachments = attachments;
		retval.blendConstants[0] = 0.0f;
		retval.blendConstants[1] = 0.0f;
		retval.blendConstants[2] = 0.0f;
		retval.blendConstants[3] = 0.0f;
		return retval;
	}

	VkPipelineDynamicStateCreateInfo CreateVkPipelineDynamicStateCreateInfo(VkDynamicState *dynamicStates, unsigned int dynamicStateCount)
	{
		VkPipelineDynamicStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		retval.dynamicStateCount = dynamicStateCount;
		retval.pDynamicStates = dynamicStates;
		return retval;
	}

	// Task B5: Adjust VkPipelineLayout creation
	void CreatePipelineLayout()
	{
		std::array<VkDescriptorSetLayout, 2> setLayouts = {descriptorSetLayout, textureDescriptorSetLayout};

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();

		if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create pipeline layout!");
		}
	}

	void BindShutdownCallback()
	{
		shutdown.Create(vlk, [&]()
						{
			if (+shutdown.Find(GW::GRAPHICS::GVulkanSurface::Events::RELEASE_RESOURCES, true)) {
				CleanUp(); // unlike D3D we must be careful about destroy timing
			} });
	}

public:
	void Render()
	{
		VkCommandBuffer commandBuffer = GetCurrentCommandBuffer();
		SetUpPipeline(commandBuffer);

		uint32_t currentImageIndex;
		if (vlk.GetSwapchainCurrentImage(currentImageIndex) != GW::GReturn::SUCCESS)
		{
			throw std::runtime_error("Failed to get current swapchain image index");
		}

		viewMatrix = FreeLookCamera(win, viewMatrix);
		UpdateUniformBuffer(currentImageIndex);

		// UpdateDescriptorSet();
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentImageIndex], 0, nullptr);
		BindTextureDescriptorSet(commandBuffer, currentImageIndex);
		VkDeviceSize offsets[] = {vertexBufferOffset};
		// VkDeviceSize vertexBufferOffset = 8; // Vertex data starts at byte offset 8 in the unified buffer
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &unifiedBufferHandle, offsets);

		// Bind the unified buffer as index buffer
		// VkDeviceSize indexBufferOffset = 0; // Index data starts at byte offset 0 in the unified buffer
		vkCmdBindIndexBuffer(commandBuffer, unifiedBufferHandle, indexBufferOffset, VK_INDEX_TYPE_UINT16);

		// Use vkCmdDrawIndexed to draw the mesh

		// std::cout << indexCount << "    " << indexBufferOffset <<	vertexBufferOffset	<<  std::endl;

		// uint32_t indexCount = 3; // Number of indices from GLTF accessor
		vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
		// vkCmdDraw(commandBuffer, 3, 1, 0, 0);
	}

private:
	VkFramebuffer CreateFramebuffer(VkImageView imageView)
	{
		VkFramebuffer framebuffer;
		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &imageView;
		framebufferInfo.width = windowWidth;
		framebufferInfo.height = windowHeight;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create framebuffer!");
		}

		return framebuffer;
	}
	VkCommandBuffer GetCurrentCommandBuffer()
	{
		unsigned int currentBuffer;
		vlk.GetSwapchainCurrentImage(currentBuffer);

		VkCommandBuffer commandBuffer;
		vlk.GetCommandBuffer(currentBuffer, (void **)&commandBuffer);
		return commandBuffer;
	}

	void SetUpPipeline(VkCommandBuffer &commandBuffer)
	{
		UpdateWindowDimensions();
		SetViewport(commandBuffer);
		SetScissor(commandBuffer);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		BindVertexBuffers(commandBuffer);
	}

	void SetViewport(const VkCommandBuffer &commandBuffer)
	{
		VkViewport viewport = CreateViewportFromWindowDimensions();
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	}

	void SetScissor(const VkCommandBuffer &commandBuffer)
	{
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	/*void BindVertexBuffers(VkCommandBuffer& commandBuffer)
	{
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &unifiedBufferHandle, offsets);
	}*/

	void BindVertexBuffers(VkCommandBuffer &commandBuffer)
	{
		std::vector<VkBuffer> buffers(4, unifiedBufferHandle);
		vkCmdBindVertexBuffers(commandBuffer, 0, 4, buffers.data(), attributeOffsets.data());
	}

	// Cleanup callback function (passed to VKSurface, will be called when the pipeline shuts down)
	void CleanUp()
	{
		// wait till everything has completed
		vkDeviceWaitIdle(device);

		// Release allocated buffers, shaders & pipeline
		vkDestroyBuffer(device, unifiedBufferHandle, nullptr);
		vkFreeMemory(device, unifiedBufferData, nullptr);

		// release textures
		for (auto &texture : textures)
		{
			vkDestroySampler(device, texture.sampler, nullptr);
			vkDestroyImageView(device, texture.imageView, nullptr);
			vkDestroyImage(device, texture.image, nullptr);
			vkFreeMemory(device, texture.memory, nullptr);
			vkDestroyBuffer(device, texture.buffer, nullptr);
		}

		for (size_t i = 0; i < uniformBuffers.size(); i++)
		{
			vkDestroyBuffer(device, uniformBuffers[i], nullptr);
			vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
		}

		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, textureDescriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyShaderModule(device, vertexShader, nullptr);

		vkDestroyShaderModule(device, fragmentShader, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
	}
};
