import { useState, useRef, useEffect } from 'react';
import { Camera, Square, Play } from 'lucide-react';
import { Button } from '@/app/components/ui/button';

interface CameraCaptureProps {
  onCapture: (imageData: string) => void;
  currentPhoto?: string;
}

export function CameraCapture({ onCapture, currentPhoto }: CameraCaptureProps) {
  const [isStreaming, setIsStreaming] = useState(false);
  const [error, setError] = useState<string>('');
  const videoRef = useRef<HTMLVideoElement>(null);
  const streamRef = useRef<MediaStream | null>(null);

  const startCamera = async () => {
    try {
      setError('');
      const stream = await navigator.mediaDevices.getUserMedia({ 
        video: { width: 640, height: 480 } 
      });
      
      if (videoRef.current) {
        videoRef.current.srcObject = stream;
        streamRef.current = stream;
        setIsStreaming(true);
      }
    } catch (err) {
      setError('Camera access denied or not available. Please ensure you are on HTTPS or localhost.');
      console.error('Camera error:', err);
    }
  };

  const stopCamera = () => {
    if (streamRef.current) {
      streamRef.current.getTracks().forEach(track => track.stop());
      streamRef.current = null;
    }
    if (videoRef.current) {
      videoRef.current.srcObject = null;
    }
    setIsStreaming(false);
  };

  const capturePhoto = () => {
    if (videoRef.current) {
      const canvas = document.createElement('canvas');
      canvas.width = videoRef.current.videoWidth;
      canvas.height = videoRef.current.videoHeight;
      const ctx = canvas.getContext('2d');
      
      if (ctx) {
        ctx.drawImage(videoRef.current, 0, 0);
        const imageData = canvas.toDataURL('image/jpeg', 0.8);
        onCapture(imageData);
        stopCamera();
      }
    }
  };

  useEffect(() => {
    return () => {
      stopCamera();
    };
  }, []);

  return (
    <div className="space-y-4">
      <div className="relative w-full aspect-video bg-gray-100 rounded-lg overflow-hidden flex items-center justify-center">
        {isStreaming ? (
          <video
            ref={videoRef}
            autoPlay
            playsInline
            className="w-full h-full object-cover"
          />
        ) : currentPhoto ? (
          <img
            src={currentPhoto}
            alt="Patient"
            className="w-full h-full object-cover"
          />
        ) : (
          <div className="text-gray-400 text-center">
            <Camera className="w-16 h-16 mx-auto mb-2" />
            <p>No photo captured</p>
          </div>
        )}
      </div>

      {error && (
        <div className="text-sm text-red-600 p-3 bg-red-50 rounded-md">
          {error}
        </div>
      )}

      <div className="flex gap-2">
        {!isStreaming ? (
          <Button
            type="button"
            onClick={startCamera}
            variant="outline"
            className="flex-1"
          >
            <Play className="w-4 h-4 mr-2" />
            Start Camera
          </Button>
        ) : (
          <>
            <Button
              type="button"
              onClick={capturePhoto}
              className="flex-1"
            >
              <Camera className="w-4 h-4 mr-2" />
              Capture Photo
            </Button>
            <Button
              type="button"
              onClick={stopCamera}
              variant="destructive"
              className="flex-1"
            >
              <Square className="w-4 h-4 mr-2" />
              Stop Camera
            </Button>
          </>
        )}
      </div>

      <p className="text-xs text-gray-500">
        * Camera feature requires HTTPS or localhost environment
      </p>
    </div>
  );
}
